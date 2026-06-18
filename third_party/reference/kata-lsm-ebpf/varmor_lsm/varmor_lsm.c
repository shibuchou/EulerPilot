// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 vArmor-ebpf Authors
//
// varmor_lsm.c — User-space loader library for the vArmor BPF LSM enforcer.
// Translated from the original Go source:
//   pkg/bpfenforcer/enforcer.go → enforcer lifecycle + map ops + audit reader
//   pkg/bpfenforcer/rule.go     → rule constructors
//   pkg/bpfenforcer/const.go    → name helpers
//   pkg/bpfenforcer/types.go    → struct definitions (in varmor_lsm.h)

#include "varmor_lsm.h"

#include <arpa/inet.h>
#include <errno.h>
#include <linux/bpf.h>
#include <linux/capability.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/syscall.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * Internal helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

static int set_error(char *errbuf, size_t errbuf_len, const char *fmt, ...)
{
    if (errbuf != NULL && errbuf_len > 0) {
        va_list args;
        va_start(args, fmt);
        vsnprintf(errbuf, errbuf_len, fmt, args);
        va_end(args);
    }
    return -1;
}

static char *reverse_string(const char *s, size_t len, char *out)
{
    for (size_t i = 0; i < len; i++)
        out[i] = s[len - i - 1];
    out[len] = '\0';
    return out;
}

static size_t count_single_star_wildcards(const char *pattern)
{
    size_t count = 0;
    size_t len = strlen(pattern);
    for (size_t i = 0; i < len; i++) {
        if (pattern[i] != '*')
            continue;
        if ((i > 0 && pattern[i - 1] == '*') ||
            (i + 1 < len && pattern[i + 1] == '*'))
            continue;
        count++;
    }
    return count;
}

static size_t count_double_star_wildcards(const char *pattern)
{
    size_t count = 0;
    for (const char *p = pattern; (p = strstr(p, "**")) != NULL; p += 2)
        count++;
    return count;
}

static int copy_pattern_part(uint8_t dest[MAX_FILE_PATH_PATTERN_LENGTH],
                             const char *part, size_t len, bool reverse,
                             const char *label, char *errbuf, size_t errbuf_len)
{
    memset(dest, 0, MAX_FILE_PATH_PATTERN_LENGTH);
    if (len == 0)
        return 0;

    if (len >= MAX_FILE_PATH_PATTERN_LENGTH) {
        char shown[MAX_FILE_PATH_PATTERN_LENGTH + 1];
        size_t shown_len = MAX_FILE_PATH_PATTERN_LENGTH;
        if (reverse) {
            char tmp[MAX_FILE_PATH_PATTERN_LENGTH + 1];
            memcpy(tmp, part, MAX_FILE_PATH_PATTERN_LENGTH);
            tmp[MAX_FILE_PATH_PATTERN_LENGTH] = '\0';
            reverse_string(tmp, shown_len, shown);
        } else {
            memcpy(shown, part, shown_len);
            shown[shown_len] = '\0';
        }
        return set_error(errbuf, errbuf_len,
                         "the length of %s '%s' should be less than the maximum (%d)",
                         label, shown, MAX_FILE_PATH_PATTERN_LENGTH);
    }

    if (reverse) {
        for (size_t i = 0; i < len; i++)
            dest[i] = (uint8_t)part[len - i - 1];
    } else {
        memcpy(dest, part, len);
    }
    return 0;
}

/*
 * fill_path_pattern — parse a file-path pattern into prefix/suffix/flags.
 *
 * Supported forms:
 *   /etc/passwd        → PreciseMatch
 *   /etc/ssl/<double-star>        → GreedyMatch (matches any depth under /etc/ssl/)
 *   *.log              → single-star wildcard on file name
 *   passwd*            → prefix match on file name
 */
static int fill_path_pattern(const char *pattern, uint32_t *flags,
                             uint8_t prefix[MAX_FILE_PATH_PATTERN_LENGTH],
                             uint8_t suffix[MAX_FILE_PATH_PATTERN_LENGTH],
                             char *errbuf, size_t errbuf_len)
{
    const char *source = pattern != NULL ? pattern : "";
    size_t single_stars = count_single_star_wildcards(source);
    size_t double_stars = count_double_star_wildcards(source);

    memset(prefix, 0, MAX_FILE_PATH_PATTERN_LENGTH);
    memset(suffix, 0, MAX_FILE_PATH_PATTERN_LENGTH);
    *flags = 0;

    if (single_stars > 0 && strstr(source, "**") != NULL)
        return set_error(errbuf, errbuf_len,
                         "the globbing * and ** cannot be used at the same time in '%s'",
                         source);

    if (single_stars > 1 || double_stars > 1)
        return set_error(errbuf, errbuf_len,
                         "the globbing * or ** can only be used once in '%s'", source);

    if (single_stars > 0) {
        const char *star = strchr(source, '*');
        if (strchr(source, '/') != NULL)
            return set_error(errbuf, errbuf_len,
                             "the pattern '%s' with globbing * is not supported", source);

        size_t prefix_len = (size_t)(star - source);
        size_t suffix_len = strlen(star + 1);
        if (prefix_len > 0) {
            if (copy_pattern_part(prefix, source, prefix_len, false,
                                  "prefix", errbuf, errbuf_len) != 0)
                return -1;
            *flags |= PREFIX_MATCH;
        }
        if (suffix_len > 0) {
            if (copy_pattern_part(suffix, star + 1, suffix_len, true,
                                  "suffix", errbuf, errbuf_len) != 0)
                return -1;
            *flags |= SUFFIX_MATCH;
        }
        return 0;
    }

    if (double_stars > 0) {
        const char *wildcard = strstr(source, "**");
        size_t prefix_len = (size_t)(wildcard - source);
        size_t suffix_len = strlen(wildcard + 2);
        *flags |= GREEDY_MATCH;

        if (prefix_len > 0) {
            if (copy_pattern_part(prefix, source, prefix_len, false,
                                  "prefix", errbuf, errbuf_len) != 0)
                return -1;
            *flags |= PREFIX_MATCH;
        }
        if (suffix_len > 0) {
            if (copy_pattern_part(suffix, wildcard + 2, suffix_len, true,
                                  "suffix", errbuf, errbuf_len) != 0)
                return -1;
            *flags |= SUFFIX_MATCH;
        }
        return 0;
    }

    if (copy_pattern_part(prefix, source, strlen(source), false,
                          "prefix", errbuf, errbuf_len) != 0)
        return -1;
    *flags |= PRECISE_MATCH | PREFIX_MATCH;
    return 0;
}

static void build_mask(uint8_t mask[IP_ADDRESS_SIZE], int prefix_len, int max_bits)
{
    memset(mask, 0, IP_ADDRESS_SIZE);
    if (prefix_len < 0)  prefix_len = 0;
    if (prefix_len > max_bits) prefix_len = max_bits;
    for (int bit = 0; bit < prefix_len; bit++)
        mask[bit / 8] |= (uint8_t)(0x80U >> (bit % 8));
}

static void apply_mask(uint8_t addr[IP_ADDRESS_SIZE],
                       const uint8_t mask[IP_ADDRESS_SIZE], int byte_count)
{
    for (int i = 0; i < byte_count; i++)
        addr[i] &= mask[i];
}

static int parse_cidr(const char *cidr, NetworkRule *rule,
                      char *errbuf, size_t errbuf_len)
{
    char addr_part[INET6_ADDRSTRLEN];
    const char *slash = strchr(cidr, '/');
    char *end = NULL;
    long prefix_len;

    if (slash == NULL)
        return set_error(errbuf, errbuf_len, "invalid CIDR address: %s", cidr);

    size_t addr_len = (size_t)(slash - cidr);
    if (addr_len == 0 || addr_len >= sizeof(addr_part))
        return set_error(errbuf, errbuf_len, "invalid CIDR address: %s", cidr);

    memcpy(addr_part, cidr, addr_len);
    addr_part[addr_len] = '\0';

    errno = 0;
    prefix_len = strtol(slash + 1, &end, 10);
    if (errno != 0 || end == slash + 1 || *end != '\0')
        return set_error(errbuf, errbuf_len, "invalid CIDR prefix in %s", cidr);

    if (inet_pton(AF_INET, addr_part, rule->address) == 1) {
        if (prefix_len < 0 || prefix_len > 32)
            return set_error(errbuf, errbuf_len,
                             "invalid IPv4 CIDR prefix in %s", cidr);
        rule->flags |= IPV4_MATCH;
        build_mask(rule->mask, (int)prefix_len, 32);
        apply_mask(rule->address, rule->mask, 4);
        return 0;
    }

    if (inet_pton(AF_INET6, addr_part, rule->address) == 1) {
        if (prefix_len < 0 || prefix_len > 128)
            return set_error(errbuf, errbuf_len,
                             "invalid IPv6 CIDR prefix in %s", cidr);
        rule->flags |= IPV6_MATCH;
        build_mask(rule->mask, (int)prefix_len, 128);
        apply_mask(rule->address, rule->mask, 16);
        return 0;
    }

    return set_error(errbuf, errbuf_len, "invalid CIDR address: %s", cidr);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Rule constructors  (translated from rule.go)
 * ═══════════════════════════════════════════════════════════════════════════ */

int varmor_new_capability_rule(uint32_t mode, uint64_t capabilities,
                               CapabilityRule *out, char *errbuf, size_t errbuf_len)
{
    (void)errbuf;
    (void)errbuf_len;
    if (out == NULL) return -1;
    memset(out, 0, sizeof(*out));
    out->mode = mode;
    out->caps = capabilities;
    return 0;
}

int varmor_new_path_rule(uint32_t mode, const char *pattern, uint32_t permissions,
                         PathRule *out, char *errbuf, size_t errbuf_len)
{
    if (out == NULL) return -1;
    memset(out, 0, sizeof(*out));
    out->mode = mode;
    if (fill_path_pattern(pattern, &out->flags, out->prefix, out->suffix,
                          errbuf, errbuf_len) != 0)
        return -1;
    out->permissions = permissions;
    return 0;
}

int varmor_new_network_connect_rule(uint32_t mode, const char *cidr,
                                    const char *ip_address, uint16_t port,
                                    uint16_t end_port, const uint16_t *ports,
                                    size_t ports_len, NetworkRule *out,
                                    char *errbuf, size_t errbuf_len)
{
    const char *cidr_value    = cidr       != NULL ? cidr       : "";
    const char *address_value = ip_address != NULL ? ip_address : "";

    if (out == NULL) return -1;

    if (cidr_value[0] == '\0' && address_value[0] == '\0' &&
        port == 0 && end_port == 0 && ports == NULL)
        return set_error(errbuf, errbuf_len,
                         "cidr, ipAddress, port, endPort and ports cannot be empty at the same time");

    if (cidr_value[0] != '\0' && address_value[0] != '\0')
        return set_error(errbuf, errbuf_len,
                         "cannot set CIDR and IP address at the same time");

    if ((port != 0 || end_port != 0) && ports != NULL)
        return set_error(errbuf, errbuf_len,
                         "cannot set port/endPort and ports at the same time");

    if (port == 0 && end_port != 0)
        return set_error(errbuf, errbuf_len,
                         "port cannot be 0 when endPort is set");

    if (end_port != 0 && end_port < port)
        return set_error(errbuf, errbuf_len,
                         "endPort cannot be less than port");

    if (ports != NULL && ports_len > MAX_PORTS_COUNT)
        return set_error(errbuf, errbuf_len,
                         "the number of ports cannot be greater than 16");

    if (ports != NULL) {
        for (size_t i = 0; i < ports_len; i++) {
            if (ports[i] == 0)
                return set_error(errbuf, errbuf_len, "invalid network port in ports");
        }
    }

    memset(out, 0, sizeof(*out));
    out->mode = mode;

    if (cidr_value[0] != '\0') {
        out->flags |= CIDR_MATCH;
        if (parse_cidr(cidr_value, out, errbuf, errbuf_len) != 0)
            return -1;
    } else if (address_value[0] == '\0') {
        out->flags |= IPV4_MATCH | IPV6_MATCH;
    } else if (strcmp(address_value, POD_SELF_IP) == 0) {
        out->flags |= POD_SELF_IP_MATCH | IPV4_MATCH | IPV6_MATCH;
    } else if (strcmp(address_value, UNSPECIFIED_ADDR) == 0) {
        out->flags |= PRECISE_MATCH | IPV4_MATCH | IPV6_MATCH;
    } else {
        out->flags |= PRECISE_MATCH;
        if (inet_pton(AF_INET, address_value, out->address) == 1)
            out->flags |= IPV4_MATCH;
        else if (inet_pton(AF_INET6, address_value, out->address) == 1)
            out->flags |= IPV6_MATCH;
        else
            return set_error(errbuf, errbuf_len,
                             "the address is not a valid textual representation of an IP address");
    }

    if (ports != NULL) {
        out->flags |= PORTS_MATCH;
        memcpy(out->ports, ports, ports_len * sizeof(uint16_t));
    } else if (port != 0 && end_port != 0) {
        out->flags |= PORT_RANGE_MATCH;
        out->port = port;
        out->end_port = end_port;
    } else if (port != 0) {
        out->flags |= PORT_MATCH;
        out->port = port;
    }

    return 0;
}

int varmor_new_network_create_rule(uint32_t mode, uint64_t domains, uint64_t types,
                                   uint64_t protocols, NetworkRule *out,
                                   char *errbuf, size_t errbuf_len)
{
    if (out == NULL) return -1;

    if (types != 0 && protocols != 0)
        return set_error(errbuf, errbuf_len,
                         "types and protocols cannot be set at the same time");
    if (domains == 0 && types == 0 && protocols == 0)
        return set_error(errbuf, errbuf_len,
                         "domains, types and protocols cannot be empty at the same time");

    memset(out, 0, sizeof(*out));
    out->mode      = mode;
    out->flags     = SOCKET_MATCH;
    out->domains   = domains;
    out->types     = types;
    out->protocols = protocols;
    return 0;
}

int varmor_new_ptrace_rule(uint32_t mode, uint32_t permissions, uint32_t flags,
                           PtraceRule *out)
{
    if (out == NULL) return -1;
    memset(out, 0, sizeof(*out));
    out->mode        = mode;
    out->permissions = permissions;
    out->flags       = flags;
    return 0;
}

int varmor_new_mount_rule(uint32_t mode, const char *source_pattern,
                          const char *fstype, uint32_t mount_flags,
                          uint32_t reverse_mount_flags, MountRule *out,
                          char *errbuf, size_t errbuf_len)
{
    const char *fs = fstype != NULL ? fstype : "";

    if (out == NULL) return -1;
    if (strlen(fs) >= MAX_FILE_SYSTEM_TYPE_LENGTH)
        return set_error(errbuf, errbuf_len,
                         "the length of fstype '%s' should be less than the maximum (%d)",
                         fs, MAX_FILE_SYSTEM_TYPE_LENGTH);

    memset(out, 0, sizeof(*out));
    out->mode = mode;
    if (fill_path_pattern(source_pattern, &out->flags, out->prefix, out->suffix,
                          errbuf, errbuf_len) != 0)
        return -1;

    out->mount_flags         = mount_flags;
    out->reverse_mount_flags = reverse_mount_flags;
    memcpy(out->fstype, fs, strlen(fs));
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Name / display helpers  (translated from const.go)
 * ═══════════════════════════════════════════════════════════════════════════ */

const char *varmor_enforcement_action_name(uint32_t action)
{
    switch (action) {
    case DENIED_ACTION:  return "DENIED";
    case AUDIT_ACTION:   return "AUDIT";
    case ALLOWED_ACTION: return "ALLOWED";
    default:             return "UNKNOWN";
    }
}

const char *varmor_event_type_name(uint32_t type)
{
    switch (type) {
    case CAPABILITY_TYPE: return "Capability";
    case FILE_TYPE:       return "File";
    case BPRM_TYPE:       return "Bprm";
    case NETWORK_TYPE:    return "Network";
    case PTRACE_TYPE:     return "Ptrace";
    case MOUNT_TYPE:      return "Mount";
    default:              return "Unknown";
    }
}

const char *varmor_capability_name(uint32_t cap)
{
    switch (cap) {
    case CAP_CHOWN:            return "chown";
    case CAP_DAC_OVERRIDE:     return "dac_override";
    case CAP_DAC_READ_SEARCH:  return "dac_read_search";
    case CAP_FOWNER:           return "fowner";
    case CAP_FSETID:           return "fsetid";
    case CAP_KILL:             return "kill";
    case CAP_SETGID:           return "setgid";
    case CAP_SETUID:           return "setuid";
    case CAP_SETPCAP:          return "setpcap";
    case CAP_LINUX_IMMUTABLE:  return "linux_immutable";
    case CAP_NET_BIND_SERVICE: return "net_bind_service";
    case CAP_NET_BROADCAST:    return "net_broadcast";
    case CAP_NET_ADMIN:        return "net_admin";
    case CAP_NET_RAW:          return "net_raw";
    case CAP_IPC_LOCK:         return "ipc_lock";
    case CAP_IPC_OWNER:        return "ipc_owner";
    case CAP_SYS_MODULE:       return "sys_module";
    case CAP_SYS_RAWIO:        return "sys_rawio";
    case CAP_SYS_CHROOT:       return "sys_chroot";
    case CAP_SYS_PTRACE:       return "sys_ptrace";
    case CAP_SYS_PACCT:        return "sys_pacct";
    case CAP_SYS_ADMIN:        return "sys_admin";
    case CAP_SYS_BOOT:         return "sys_boot";
    case CAP_SYS_NICE:         return "sys_nice";
    case CAP_SYS_RESOURCE:     return "sys_resource";
    case CAP_SYS_TIME:         return "sys_time";
    case CAP_SYS_TTY_CONFIG:   return "sys_tty_config";
    case CAP_MKNOD:            return "mknod";
    case CAP_LEASE:            return "lease";
    case CAP_AUDIT_WRITE:      return "audit_write";
    case CAP_AUDIT_CONTROL:    return "audit_control";
    case CAP_SETFCAP:          return "setfcap";
#ifdef CAP_MAC_OVERRIDE
    case CAP_MAC_OVERRIDE:     return "mac_override";
#endif
#ifdef CAP_MAC_ADMIN
    case CAP_MAC_ADMIN:        return "mac_admin";
#endif
#ifdef CAP_SYSLOG
    case CAP_SYSLOG:           return "syslog";
#endif
#ifdef CAP_WAKE_ALARM
    case CAP_WAKE_ALARM:       return "wake_alarm";
#endif
#ifdef CAP_BLOCK_SUSPEND
    case CAP_BLOCK_SUSPEND:    return "block_suspend";
#endif
#ifdef CAP_AUDIT_READ
    case CAP_AUDIT_READ:       return "audit_read";
#endif
#ifdef CAP_PERFMON
    case CAP_PERFMON:          return "perfmon";
#endif
#ifdef CAP_BPF
    case CAP_BPF:              return "bpf";
#endif
#ifdef CAP_CHECKPOINT_RESTORE
    case CAP_CHECKPOINT_RESTORE: return "checkpoint_restore";
#endif
    default:                   return "unknown";
    }
}

const char *varmor_path_permission_name(uint32_t perm)
{
    switch (perm) {
    case AA_MAY_EXEC:   return "exec";
    case AA_MAY_WRITE:  return "write";
    case AA_MAY_READ:   return "read";
    case AA_MAY_APPEND: return "append";
    case AA_MAY_CREATE: return "create";
    case AA_MAY_RENAME: return "rename";
    case AA_MAY_LINK:   return "link";
    default:            return "unknown";
    }
}

const char *varmor_socket_domain_name(uint32_t domain)
{
    switch (domain) {
    case AF_UNSPEC:    return "AF_UNSPEC";
    case AF_UNIX:      return "AF_UNIX";
    case AF_INET:      return "AF_INET";
    case AF_INET6:     return "AF_INET6";
#ifdef AF_NETLINK
    case AF_NETLINK:   return "AF_NETLINK";
#endif
#ifdef AF_PACKET
    case AF_PACKET:    return "AF_PACKET";
#endif
#ifdef AF_VSOCK
    case AF_VSOCK:     return "AF_VSOCK";
#endif
#ifdef AF_BLUETOOTH
    case AF_BLUETOOTH: return "AF_BLUETOOTH";
#endif
    default:           return "UNKNOWN";
    }
}

const char *varmor_socket_type_name(uint32_t type)
{
    switch (type) {
    case SOCK_STREAM:    return "SOCK_STREAM";
    case SOCK_DGRAM:     return "SOCK_DGRAM";
    case SOCK_RAW:       return "SOCK_RAW";
    case SOCK_RDM:       return "SOCK_RDM";
    case SOCK_SEQPACKET: return "SOCK_SEQPACKET";
    default:             return "UNKNOWN";
    }
}

const char *varmor_socket_protocol_name(uint32_t proto)
{
    switch (proto) {
    case IPPROTO_IP:   return "IPPROTO_IP";
    case IPPROTO_ICMP: return "IPPROTO_ICMP";
    case IPPROTO_TCP:  return "IPPROTO_TCP";
    case IPPROTO_UDP:  return "IPPROTO_UDP";
    case IPPROTO_IPV6: return "IPPROTO_IPV6";
    case IPPROTO_RAW:  return "IPPROTO_RAW";
    case IPPROTO_SCTP: return "IPPROTO_SCTP";
    default:           return "UNKNOWN";
    }
}

const char *varmor_ptrace_permission_name(uint32_t perm)
{
    switch (perm) {
    case AA_PTRACE_TRACE: return "trace";
    case AA_PTRACE_READ:  return "read";
    case AA_MAY_BE_TRACED: return "traceby";
    case AA_MAY_BE_READ:   return "readby";
    default:              return "unknown";
    }
}

const char *varmor_mount_flag_name(uint32_t flag)
{
    switch (flag) {
    case MS_RDONLY:      return "ro";
    case MS_NOSUID:      return "nosuid";
    case MS_NODEV:       return "nodev";
    case MS_NOEXEC:      return "noexec";
    case MS_SYNCHRONOUS: return "sync";
    case MS_REMOUNT:     return "remount";
    case MS_MANDLOCK:    return "mand";
    case MS_DIRSYNC:     return "dirsync";
    case AA_MAY_UMOUNT:  return "umount";
    case MS_NOATIME:     return "noatime";
    case MS_NODIRATIME:  return "nodiratime";
    case MS_MOVE:        return "move";
    case MS_SILENT:      return "silent";
    case MS_UNBINDABLE:  return "make-unbindable";
    case MS_PRIVATE:     return "make-private";
    case MS_SLAVE:       return "make-slave";
    case MS_SHARED:      return "make-shared";
    case MS_RELATIME:    return "relatime";
    case MS_I_VERSION:   return "iversion";
    case MS_STRICTATIME: return "strictatime";
    default:             return "unknown";
    }
}

const char *varmor_mount_bind_flag_name(uint32_t flags)
{
    if (flags == (MS_BIND | MS_REC | MS_UNBINDABLE)) return "make-runbindable";
    if (flags == (MS_BIND | MS_REC | MS_PRIVATE))    return "make-rprivate";
    if (flags == (MS_BIND | MS_REC | MS_SLAVE))      return "make-rslave";
    if (flags == (MS_BIND | MS_REC | MS_SHARED))     return "make-rshared";
    if (flags == (MS_BIND | MS_REC))                 return "rbind";
    if (flags == MS_BIND)                            return "bind";
    return NULL;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * BPF map helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

static int map_delete(int fd, const uint32_t *key)
{
    int ret = bpf_map_delete_elem(fd, key);
    if (ret != 0 && errno == ENOENT)
        return 0;
    return ret;
}

int create_inner_hash_map(const char *name, uint32_t value_size,
                                 uint32_t max_entries)
{
    union bpf_attr attr;

    memset(&attr, 0, sizeof(attr));
    attr.map_type = BPF_MAP_TYPE_HASH;
    attr.key_size = sizeof(uint32_t);
    attr.value_size = value_size;
    attr.max_entries = max_entries;

    if (name != NULL) {
        strncpy((char *)attr.map_name, name, BPF_OBJ_NAME_LEN - 1);
        attr.map_name[BPF_OBJ_NAME_LEN - 1] = '\0';
    }

    return syscall(__NR_bpf, BPF_MAP_CREATE, &attr, sizeof(attr));
}

static int set_memlock_rlimit(void)
{
    struct rlimit rlim = { .rlim_cur = RLIM_INFINITY, .rlim_max = RLIM_INFINITY };
    return setrlimit(RLIMIT_MEMLOCK, &rlim);
}

static void destroy_link(struct bpf_link **link)
{
    if (link != NULL && *link != NULL) {
        bpf_link__destroy(*link);
        *link = NULL;
    }
}

static int attach_lsm_program(struct bpf_program *prog, struct bpf_link **link)
{
    if (prog == NULL || link == NULL) return -1;
    // Skip programs not loaded (autoload=false) — fd will be < 0
    if (bpf_program__fd(prog) < 0) return 0;
    *link = bpf_program__attach(prog);
    if (*link == NULL) return -1;
    if (bpf_link__fd(*link) < 0) {
        destroy_link(link);
        return -1;
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Enforcer lifecycle  (translated from enforcer.go)
 * ═══════════════════════════════════════════════════════════════════════════ */

VarmorEnforcer *varmor_enforcer_new(void)
{
    return calloc(1, sizeof(VarmorEnforcer));
}

void varmor_enforcer_free(VarmorEnforcer *e)
{
    if (e == NULL) return;
    varmor_enforcer_remove(e);
    free(e);
}

static int get_map_fd(struct bpf_object *obj, const char *name)
{
    struct bpf_map *map = bpf_object__find_map_by_name(obj, name);
    if (map == NULL) return -1;
    return bpf_map__fd(map);
}

static struct bpf_program *get_prog(struct bpf_object *obj, const char *name)
{
    return bpf_object__find_program_by_name(obj, name);
}

static void disable_prog_autoload(struct bpf_object *obj, const char *name)
{
    struct bpf_program *prog = bpf_object__find_program_by_name(obj, name);
    if (prog != NULL) {
        bpf_program__set_autoload(prog, false);
    }
}

static int init_file_progs(struct bpf_object *obj)
{
    struct bpf_map *map = bpf_object__find_map_by_name(obj, "file_progs");
    struct bpf_program *p0 = bpf_object__find_program_by_name(obj, "varmor_path_link_tail");
    struct bpf_program *p1 = bpf_object__find_program_by_name(obj, "varmor_path_rename_tail");

    if (map == NULL || p0 == NULL || p1 == NULL) {
        fprintf(stderr, "varmor: failed to find file_progs or tail programs\n");
        return -1;
    }

    int map_fd = bpf_map__fd(map);
    int fd0 = bpf_program__fd(p0);
    int fd1 = bpf_program__fd(p1);

    if (map_fd < 0 || fd0 < 0 || fd1 < 0) {
        fprintf(stderr, "varmor: invalid fd for file_progs or tail programs\n");
        return -1;
    }

    uint32_t k0 = 0;
    uint32_t k1 = 1;

    if (bpf_map_update_elem(map_fd, &k0, &fd0, BPF_ANY) != 0) {
        fprintf(stderr, "varmor: failed to update file_progs[0]: %s\n", strerror(errno));
        return -1;
    }

    if (bpf_map_update_elem(map_fd, &k1, &fd1, BPF_ANY) != 0) {
        fprintf(stderr, "varmor: failed to update file_progs[1]: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}


// ── Check if BPF is already pinned (from previous agent run) ──
static bool varmor_bpf_is_pinned(void)
{
    struct stat st;
    return stat(PIN_PATH, &st) == 0 && S_ISDIR(st.st_mode);
}

// ── Reuse pinned BPF programs ──
static int varmor_enforcer_reuse_pinned(VarmorEnforcer *e)
{
    e->obj = bpf_object__open_file(PIN_PATH "/programs", NULL);
    if (!e->obj) return -1;
    
    // Get map fds from pinned maps
    e->map_profile_mode_fd = bpf_obj_get(PIN_PATH "/v_profile_mode");
    e->map_capable_fd      = bpf_obj_get(PIN_PATH "/v_capable");
    e->map_file_outer_fd   = bpf_obj_get(PIN_PATH "/v_file_outer");
    e->map_bprm_outer_fd   = bpf_obj_get(PIN_PATH "/v_bprm_outer");
    e->map_net_outer_fd    = bpf_obj_get(PIN_PATH "/v_net_outer");
    e->map_pod_ip_fd       = bpf_obj_get(PIN_PATH "/v_pod_ip");
    e->map_ptrace_fd       = bpf_obj_get(PIN_PATH "/v_ptrace");
    e->map_mount_outer_fd  = bpf_obj_get(PIN_PATH "/v_mount_outer");
    e->map_audit_rb_fd     = bpf_obj_get(PIN_PATH "/v_audit_rb");
    
    return 0;
}
// Load BPF from embedded byte array (no external .bpf.o required)
int varmor_enforcer_init_embedded(VarmorEnforcer *e, const void *data, size_t data_sz)
{
    struct bpf_object *obj;
    int err;

    if (e == NULL) return -1;

    if (set_memlock_rlimit() != 0) {
        fprintf(stderr, "varmor: failed to remove memlock rlimit\n");
        return -1;
    }

    obj = bpf_object__open_mem(data, data_sz, NULL);
    if (obj == NULL) {
        fprintf(stderr, "varmor: failed to open BPF from memory\n");
        return -1;
    }

    disable_prog_autoload(obj, "varmor_path_link");
    disable_prog_autoload(obj, "varmor_path_rename");
    disable_prog_autoload(obj, "varmor_path_link_tail");
    disable_prog_autoload(obj, "varmor_path_symlink");
    disable_prog_autoload(obj, "varmor_path_rename_tail");

    // Set up inner map templates for HASH_OF_MAPS
    int inner_fds[4] = {-1, -1, -1, -1};
    {
        const char *outer_maps[] = {"v_file_outer", "v_bprm_outer", "v_net_outer", "v_mount_outer"};
        int inner_value_sizes[] = {132, 132, 144, 140};
        for (int j = 0; j < 4; j++) {
            struct bpf_map *outer = bpf_object__find_map_by_name(obj, outer_maps[j]);
            if (outer && bpf_map__type(outer) == 13) {
                LIBBPF_OPTS(bpf_map_create_opts, im_opts);
                int inner_fd = bpf_map_create(BPF_MAP_TYPE_HASH, "inner_tmpl",
                                              sizeof(uint32_t), inner_value_sizes[j], 50, &im_opts);
                if (inner_fd >= 0) {
                    bpf_map__set_inner_map_fd(outer, inner_fd);
                    inner_fds[j] = inner_fd;
                }
            }
        }
    }

    err = bpf_object__load(obj);
    for (int j = 0; j < 4; j++) { if (inner_fds[j] >= 0) close(inner_fds[j]); }
    if (err != 0) {
        fprintf(stderr, "varmor: failed to load BPF object: %d (%s)\n", err, strerror(-err));
        bpf_object__close(obj);
        return -1;
    }

    e->obj = obj;
    e->prog_capable               = get_prog(obj, "varmor_capable");
    e->prog_file_open             = get_prog(obj, "varmor_file_open");
    e->prog_path_symlink          = get_prog(obj, "varmor_path_symlink");
    e->prog_path_link             = get_prog(obj, "varmor_path_link");
    e->prog_path_rename           = get_prog(obj, "varmor_path_rename");
    e->prog_bprm_check_security   = get_prog(obj, "varmor_bprm_check_security");
    e->prog_socket_connect        = get_prog(obj, "varmor_socket_connect");
    e->prog_socket_create         = get_prog(obj, "varmor_socket_create");
    e->prog_ptrace_access_check   = get_prog(obj, "varmor_ptrace_access_check");
    e->prog_mount                 = get_prog(obj, "varmor_mount");
    e->prog_move_mount            = get_prog(obj, "varmor_move_mount");
    e->prog_umount                = get_prog(obj, "varmor_umount");
    e->map_profile_mode_fd = get_map_fd(obj, "v_profile_mode");
    e->map_capable_fd      = get_map_fd(obj, "v_capable");
    e->map_file_outer_fd   = get_map_fd(obj, "v_file_outer");
    e->map_bprm_outer_fd   = get_map_fd(obj, "v_bprm_outer");
    e->map_net_outer_fd    = get_map_fd(obj, "v_net_outer");
    e->map_pod_ip_fd       = get_map_fd(obj, "v_pod_ip");
    e->map_ptrace_fd       = get_map_fd(obj, "v_ptrace");
    e->map_mount_outer_fd  = get_map_fd(obj, "v_mount_outer");
    e->map_audit_rb_fd     = get_map_fd(obj, "v_audit_rb");
    return 0;
}

int varmor_enforcer_init(VarmorEnforcer *e, const char *bpf_object_path)
{
    struct bpf_object *obj;
    int err;

    if (e == NULL) return -1;

    if (set_memlock_rlimit() != 0) {
        fprintf(stderr, "varmor: failed to remove memlock rlimit\n");
        return -1;
    }

    obj = bpf_object__open_file(bpf_object_path, NULL);
    if (obj == NULL) {
        fprintf(stderr, "varmor: failed to open BPF object: %s\n", bpf_object_path);
        return -1;
    }

    /*
     * Disable link/rename hooks for now.
     * Current demo policy does not depend on path_link/path_rename.
     */
    disable_prog_autoload(obj, "varmor_path_link");
    disable_prog_autoload(obj, "varmor_path_rename");
    disable_prog_autoload(obj, "varmor_path_link_tail");
    disable_prog_autoload(obj, "varmor_path_symlink");
    disable_prog_autoload(obj, "varmor_path_rename_tail");


    // Set up inner map templates for HASH_OF_MAPS maps
    // IMPORTANT: do NOT close inner_fd — libbpf uses it during bpf_object__load
    // and closing it would make the fd invalid (reused by other maps)
    int inner_fds[4] = {-1, -1, -1, -1};
    {
        const char *outer_maps[] = {"v_file_outer", "v_bprm_outer", "v_net_outer", "v_mount_outer"};
        uint32_t inner_value_sizes[] = {
            PATH_RULE_SIZE,
            PATH_RULE_SIZE,
            NET_RULE_SIZE,
            MOUNT_RULE_SIZE
        };
        for (int j = 0; j < 4; j++) {
            struct bpf_map *outer = bpf_object__find_map_by_name(obj, outer_maps[j]);
            if (outer && bpf_map__type(outer) == 13) {
                LIBBPF_OPTS(bpf_map_create_opts, mopts);
                int inner_fd = bpf_map_create(BPF_MAP_TYPE_HASH, "inner_tmpl",
                                              sizeof(uint32_t),
                                              inner_value_sizes[j],
                                              50, &mopts);
                if (inner_fd >= 0) {
                    bpf_map__set_inner_map_fd(outer, inner_fd);
                    inner_fds[j] = inner_fd;  // save for cleanup
                }
            }
        }
    }
    err = bpf_object__load(obj);
    if (err != 0) {
        fprintf(stderr, "varmor: failed to load BPF object: %d\n", err);
        bpf_object__close(obj);
        return -1;
    }

    /* init_file_progs skipped because link/rename tail programs are disabled. */

    e->obj = obj;

    /* Resolve programs */
    e->prog_capable               = get_prog(obj, "varmor_capable");
    e->prog_file_open             = get_prog(obj, "varmor_file_open");
    e->prog_path_symlink          = get_prog(obj, "varmor_path_symlink");
    e->prog_path_link             = get_prog(obj, "varmor_path_link");
    e->prog_path_rename           = get_prog(obj, "varmor_path_rename");
    e->prog_bprm_check_security   = get_prog(obj, "varmor_bprm_check_security");
    e->prog_socket_connect        = get_prog(obj, "varmor_socket_connect");
    e->prog_socket_create         = get_prog(obj, "varmor_socket_create");
    e->prog_ptrace_access_check   = get_prog(obj, "varmor_ptrace_access_check");
    e->prog_mount                 = get_prog(obj, "varmor_mount");
    e->prog_move_mount            = get_prog(obj, "varmor_move_mount");
    e->prog_umount                = get_prog(obj, "varmor_umount");

    /* Resolve map fds */
    e->map_profile_mode_fd = get_map_fd(obj, "v_profile_mode");
    e->map_capable_fd      = get_map_fd(obj, "v_capable");
    e->map_file_outer_fd   = get_map_fd(obj, "v_file_outer");
    e->map_bprm_outer_fd   = get_map_fd(obj, "v_bprm_outer");
    e->map_net_outer_fd    = get_map_fd(obj, "v_net_outer");
    e->map_pod_ip_fd       = get_map_fd(obj, "v_pod_ip");
    e->map_ptrace_fd       = get_map_fd(obj, "v_ptrace");
    e->map_mount_outer_fd  = get_map_fd(obj, "v_mount_outer");
    e->map_audit_rb_fd     = get_map_fd(obj, "v_audit_rb");

    return 0;
}

int varmor_enforcer_remove(VarmorEnforcer *e)
{
    if (e == NULL) return -1;
    varmor_enforcer_stop(e);
    if (e->obj != NULL) {
        bpf_object__close(e->obj);
        e->obj = NULL;
    }
    unlink(AUDIT_RINGBUF_PIN_PATH);
    rmdir(PIN_PATH);
    return 0;
}

int varmor_enforcer_start(VarmorEnforcer *e)
{
    if (e == NULL) return -1;

    if (attach_lsm_program(e->prog_capable,             &e->link_capable) != 0) goto fail;
    if (attach_lsm_program(e->prog_file_open,           &e->link_file_open) != 0) goto fail;
    if (attach_lsm_program(e->prog_path_symlink,        &e->link_path_symlink) != 0) goto fail;
    /* path_link/path_rename disabled for this demo policy */
    if (attach_lsm_program(e->prog_bprm_check_security, &e->link_bprm) != 0) goto fail;
    if (attach_lsm_program(e->prog_socket_connect,      &e->link_sock_conn) != 0) goto fail;
    if (attach_lsm_program(e->prog_socket_create,       &e->link_socket) != 0) goto fail;
    if (attach_lsm_program(e->prog_ptrace_access_check, &e->link_ptrace) != 0) goto fail;
    if (attach_lsm_program(e->prog_mount,               &e->link_mount) != 0) goto fail;
    if (attach_lsm_program(e->prog_move_mount,          &e->link_move_mount) != 0) goto fail;
    if (attach_lsm_program(e->prog_umount,              &e->link_umount) != 0) goto fail;

    return 0;

fail:
    varmor_enforcer_stop(e);
    return -1;
}

void varmor_enforcer_stop(VarmorEnforcer *e)
{
    if (e == NULL) return;
    destroy_link(&e->link_capable);
    destroy_link(&e->link_file_open);
    destroy_link(&e->link_path_symlink);
    destroy_link(&e->link_path_link);
    destroy_link(&e->link_path_rename);
    destroy_link(&e->link_bprm);
    destroy_link(&e->link_sock_conn);
    destroy_link(&e->link_socket);
    destroy_link(&e->link_ptrace);
    destroy_link(&e->link_mount);
    destroy_link(&e->link_move_mount);
    destroy_link(&e->link_umount);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Map operations  (translated from enforcer.go)
 * ═══════════════════════════════════════════════════════════════════════════ */

int varmor_set_profile_mode(VarmorEnforcer *e, uint32_t mnt_ns_id,
                            uint32_t profile_mode)
{
    if (e == NULL) return -1;
    return bpf_map_update_elem(e->map_profile_mode_fd, &mnt_ns_id,
                               &profile_mode, BPF_ANY);
}

int varmor_clear_profile_mode(VarmorEnforcer *e, uint32_t mnt_ns_id)
{
    if (e == NULL) return -1;
    return map_delete(e->map_profile_mode_fd, &mnt_ns_id);
}

int varmor_set_capable(VarmorEnforcer *e, uint32_t mnt_ns_id,
                       const CapabilityRule *rule)
{
    if (e == NULL || rule == NULL) return -1;
    return bpf_map_update_elem(e->map_capable_fd, &mnt_ns_id, rule, BPF_ANY);
}

int varmor_clear_capable(VarmorEnforcer *e, uint32_t mnt_ns_id)
{
    if (e == NULL) return -1;
    return map_delete(e->map_capable_fd, &mnt_ns_id);
}

static int update_outer_with_one_rule(int outer_fd, uint32_t mnt_ns_id,
                                      const char *name_prefix, uint32_t value_size,
                                      uint32_t max_entries, const void *rule)
{
    char map_name[64];
    uint32_t index = 0;
    int inner_fd;
    int ret;

    snprintf(map_name, sizeof(map_name), "%s%u", name_prefix, mnt_ns_id);
    inner_fd = create_inner_hash_map(map_name, value_size, max_entries);
    if (inner_fd < 0) return -1;

    ret = bpf_map_update_elem(inner_fd, &index, rule, BPF_ANY);
    if (ret == 0)
        ret = bpf_map_update_elem(outer_fd, &mnt_ns_id, &inner_fd, BPF_ANY);

    close(inner_fd);
    return ret;
}

int varmor_set_file(VarmorEnforcer *e, uint32_t mnt_ns_id, const PathRule *rule)
{
    if (e == NULL || rule == NULL) return -1;
    return update_outer_with_one_rule(e->map_file_outer_fd, mnt_ns_id,
                                      "v_file_inner_", PATH_RULE_SIZE,
                                      MAX_BPF_FILE_RULE_COUNT, rule);
}

int varmor_clear_file(VarmorEnforcer *e, uint32_t mnt_ns_id)
{
    if (e == NULL) return -1;
    return map_delete(e->map_file_outer_fd, &mnt_ns_id);
}

int varmor_set_bprm(VarmorEnforcer *e, uint32_t mnt_ns_id, const PathRule *rule)
{
    if (e == NULL || rule == NULL) return -1;
    return update_outer_with_one_rule(e->map_bprm_outer_fd, mnt_ns_id,
                                      "v_bprm_inner_", PATH_RULE_SIZE,
                                      MAX_BPF_BPRM_RULE_COUNT, rule);
}

int varmor_clear_bprm(VarmorEnforcer *e, uint32_t mnt_ns_id)
{
    if (e == NULL) return -1;
    return map_delete(e->map_bprm_outer_fd, &mnt_ns_id);
}

int varmor_set_network(VarmorEnforcer *e, uint32_t mnt_ns_id,
                       const NetworkRule *rules, size_t rule_count)
{
    char map_name[64];
    int inner_fd;
    int ret = 0;

    if (e == NULL || (rules == NULL && rule_count != 0)) return -1;

    snprintf(map_name, sizeof(map_name), "v_net_inner_%u", mnt_ns_id);
    inner_fd = create_inner_hash_map(map_name, NET_RULE_SIZE,
                                     MAX_BPF_NETWORK_RULE_COUNT);
    if (inner_fd < 0) return -1;

    for (size_t i = 0; i < rule_count; i++) {
        uint32_t index = (uint32_t)i;
        ret = bpf_map_update_elem(inner_fd, &index, &rules[i], BPF_ANY);
        if (ret != 0) break;
    }

    if (ret == 0)
        ret = bpf_map_update_elem(e->map_net_outer_fd, &mnt_ns_id,
                                  &inner_fd, BPF_ANY);

    close(inner_fd);
    return ret;
}

int varmor_clear_network(VarmorEnforcer *e, uint32_t mnt_ns_id)
{
    if (e == NULL) return -1;
    return map_delete(e->map_net_outer_fd, &mnt_ns_id);
}

int varmor_set_pod_ips(VarmorEnforcer *e, uint32_t mnt_ns_id,
                       const char *const *addresses, size_t address_count)
{
    PodIp pod_ip;

    if (e == NULL || (addresses == NULL && address_count != 0)) return -1;
    if (address_count > 2) { errno = EINVAL; return -1; }

    memset(&pod_ip, 0, sizeof(pod_ip));
    for (size_t i = 0; i < address_count; i++) {
        if (inet_pton(AF_INET, addresses[i], pod_ip.ipv4) == 1)
            pod_ip.flags |= IPV4_MATCH;
        else if (inet_pton(AF_INET6, addresses[i], pod_ip.ipv6) == 1)
            pod_ip.flags |= IPV6_MATCH;
        else
            { errno = EINVAL; return -1; }
    }

    return bpf_map_update_elem(e->map_pod_ip_fd, &mnt_ns_id, &pod_ip, BPF_ANY);
}

int varmor_remove_pod_ips(VarmorEnforcer *e, uint32_t mnt_ns_id)
{
    if (e == NULL) return -1;
    return map_delete(e->map_pod_ip_fd, &mnt_ns_id);
}

int varmor_set_ptrace(VarmorEnforcer *e, uint32_t mnt_ns_id,
                      const PtraceRule *rule)
{
    if (e == NULL || rule == NULL) return -1;
    return bpf_map_update_elem(e->map_ptrace_fd, &mnt_ns_id, rule, BPF_ANY);
}

int varmor_clear_ptrace(VarmorEnforcer *e, uint32_t mnt_ns_id)
{
    if (e == NULL) return -1;
    return map_delete(e->map_ptrace_fd, &mnt_ns_id);
}

int varmor_set_mount(VarmorEnforcer *e, uint32_t mnt_ns_id,
                     const MountRule *rule)
{
    if (e == NULL || rule == NULL) return -1;
    return update_outer_with_one_rule(e->map_mount_outer_fd, mnt_ns_id,
                                      "v_mount_inner_", MOUNT_RULE_SIZE,
                                      MAX_BPF_MOUNT_RULE_COUNT, rule);
}

int varmor_clear_mount(VarmorEnforcer *e, uint32_t mnt_ns_id)
{
    if (e == NULL) return -1;
    return map_delete(e->map_mount_outer_fd, &mnt_ns_id);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Audit event reading  (translated from enforcer.go)
 * ═══════════════════════════════════════════════════════════════════════════ */

int varmor_load_audit_ringbuf(int *ringbuf_fd)
{
    int fd;
    if (ringbuf_fd == NULL) return -1;
    fd = bpf_obj_get(AUDIT_RINGBUF_PIN_PATH);
    if (fd < 0) return -1;
    *ringbuf_fd = fd;
    return 0;
}

static void print_string_field(const char *label, const uint8_t *bytes, size_t len)
{
    char buf[4097];
    size_t n = 0;
    while (n < len && bytes[n] != '\0') n++;
    if (n >= sizeof(buf)) n = sizeof(buf) - 1;
    memcpy(buf, bytes, n);
    buf[n] = '\0';
    printf("%s%s\n", label, buf);
}

static int audit_event_callback(void *ctx, void *data, size_t data_sz)
{
    (void)ctx;
    if (data_sz < EVENT_HEADER_SIZE) return 0;

    const EventHeader *header = (const EventHeader *)data;
    const uint8_t     *body   = (const uint8_t *)data + EVENT_HEADER_SIZE;
    size_t             body_len = data_sz - EVENT_HEADER_SIZE;

    printf("Action: %s\n",                varmor_enforcement_action_name(header->action));
    printf("Event Type: %s\n",            varmor_event_type_name(header->type));
    printf("Mount Namespace ID: %u\n",    header->mnt_ns);
    printf("PID: %u\n",                   header->tgid);
    printf("Ktime: %llu\n",               (unsigned long long)header->ktime);

    switch (header->type) {
    case CAPABILITY_TYPE:
        if (body_len >= sizeof(CapabilityEvent)) {
            const CapabilityEvent *event = (const CapabilityEvent *)body;
            printf("Capability: %s\n", varmor_capability_name(event->capability));
        }
        break;

    case FILE_TYPE:
    case BPRM_TYPE:
        if (body_len >= sizeof(PathEvent)) {
            const PathEvent *event = (const PathEvent *)body;
            printf("Permissions:");
            if (header->type == BPRM_TYPE) {
                printf(" %s", varmor_path_permission_name(event->permissions));
            } else {
                uint32_t perms[] = {
                    AA_MAY_EXEC, AA_MAY_WRITE, AA_MAY_READ, AA_MAY_APPEND,
                    AA_MAY_CREATE, AA_MAY_RENAME, AA_MAY_LINK,
                };
                for (size_t i = 0; i < sizeof(perms) / sizeof(perms[0]); i++) {
                    if ((event->permissions & perms[i]) != 0)
                        printf(" %s", varmor_path_permission_name(perms[i]));
                }
            }
            printf("\n");
            print_string_field("Path: ", event->path, sizeof(event->path));
        }
        break;

    case NETWORK_TYPE:
        if (body_len >= sizeof(NetworkEvent)) {
            const NetworkEvent *event = (const NetworkEvent *)body;
            if (event->type == CONNECT_TYPE) {
                char addrbuf[INET6_ADDRSTRLEN];
                if (event->addr.sa_family == AF_INET) {
                    struct in_addr addr = { .s_addr = event->addr.sin_addr };
                    inet_ntop(AF_INET, &addr, addrbuf, sizeof(addrbuf));
                    printf("Egress IPv4 address: %s\n", addrbuf);
                } else {
                    inet_ntop(AF_INET6, event->addr.sin6_addr, addrbuf,
                              sizeof(addrbuf));
                    printf("Egress IPv6 address: %s\n", addrbuf);
                }
                printf("Egress Port: %u\n", event->addr.port);
            } else if (event->type == SOCKET_TYPE) {
                printf("Socket Domain: %s\n",
                       varmor_socket_domain_name(event->socket.domain));
                printf("Socket Type: %s\n",
                       varmor_socket_type_name(event->socket.type));
                printf("Socket Protocol: %s\n",
                       varmor_socket_protocol_name(event->socket.protocol));
            }
        }
        break;

    case PTRACE_TYPE:
        if (body_len >= sizeof(PtraceEvent)) {
            const PtraceEvent *event = (const PtraceEvent *)body;
            printf("Permission: %s\n",
                   varmor_ptrace_permission_name(event->permission));
            printf("External: %s\n", event->external ? "true" : "false");
        }
        break;

    case MOUNT_TYPE:
        if (body_len >= sizeof(MountEvent)) {
            const MountEvent *event = (const MountEvent *)body;
            uint32_t flags[] = {
                MS_RDONLY, MS_NOSUID, MS_NODEV, MS_NOEXEC, MS_SYNCHRONOUS,
                MS_REMOUNT, MS_MANDLOCK, MS_DIRSYNC, AA_MAY_UMOUNT, MS_NOATIME,
                MS_NODIRATIME, MS_MOVE, MS_SILENT, MS_UNBINDABLE, MS_PRIVATE,
                MS_SLAVE, MS_SHARED, MS_RELATIME, MS_I_VERSION, MS_STRICTATIME,
            };
            print_string_field("Path: ", event->path, sizeof(event->path));
            print_string_field("FileSystem Type: ", event->type, sizeof(event->type));
            printf("Flags:");
            for (size_t i = 0; i < sizeof(flags) / sizeof(flags[0]); i++) {
                if ((event->flags & flags[i]) != 0)
                    printf(" %s", varmor_mount_flag_name(flags[i]));
            }
            const char *bind = varmor_mount_bind_flag_name(event->flags);
            if (bind != NULL) printf(" %s", bind);
            printf("\n");
        }
        break;
    }

    return 0;
}

int varmor_read_audit_events(int ringbuf_fd)
{
    struct ring_buffer *rb = ring_buffer__new(ringbuf_fd,
                                              audit_event_callback, NULL, NULL);
    int err;

    if (rb == NULL) return -1;

    printf("[+] Waiting for events..\n");
    while ((err = ring_buffer__poll(rb, -1)) >= 0) { }

    ring_buffer__free(rb);
    return err;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Utilities
 * ═══════════════════════════════════════════════════════════════════════════ */

int varmor_read_mnt_ns_id(uint32_t pid, uint32_t *mnt_ns_id,
                          char *errbuf, size_t errbuf_len)
{
    char path[64];
    char link[64];
    ssize_t n;

    if (mnt_ns_id == NULL) return -1;

    snprintf(path, sizeof(path), "/proc/%u/ns/mnt", pid);
    n = readlink(path, link, sizeof(link) - 1);
    if (n < 0) {
        return set_error(errbuf, errbuf_len,
                         "failed to read mnt ns link for pid %u: %s",
                         pid, strerror(errno));
    }
    link[n] = '\0';

    /* Expected format: "mnt:[1234567890]" */
    char *start = strchr(link, '[');
    char *end   = strchr(link, ']');
    if (start == NULL || end == NULL || end <= start + 1) {
        return set_error(errbuf, errbuf_len,
                         "fatal: cannot parse mnt ns id from: %s", link);
    }
    *end = '\0';

    char *err = NULL;
    unsigned long long val = strtoull(start + 1, &err, 10);
    if (err == NULL || *err != '\0') {
        return set_error(errbuf, errbuf_len,
                         "fatal: cannot convert mnt ns id (%s) to integer",
                         start + 1);
    }

    *mnt_ns_id = (uint32_t)val;
    return 0;
}
