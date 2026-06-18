// SPDX-License-Identifier: GPL-2.0
// Copyright 2024 vArmor-ebpf Authors
//
// varmor_lsm.h — Shared types, constants, and API for vArmor BPF LSM enforcer.
// Used by both the BPF kernel-side (varmor_lsm.bpf.c) and the user-space loader
// (varmor_lsm.c).

#ifndef VARMOR_LSM_H
#define VARMOR_LSM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ──────────── constants ──────────── */

#define MAX_BPF_FILE_RULE_COUNT      50
#define MAX_BPF_BPRM_RULE_COUNT      50
#define MAX_BPF_NETWORK_RULE_COUNT   50
#define MAX_BPF_MOUNT_RULE_COUNT     50

#define MAX_FILE_PATH_PATTERN_LENGTH 64
#define PATH_PATTERN_SIZE            (4 + MAX_FILE_PATH_PATTERN_LENGTH * 2)
#define PATH_RULE_SIZE               (4 * 2 + PATH_PATTERN_SIZE)

#define MAX_FILE_SYSTEM_TYPE_LENGTH  16
#define MOUNT_RULE_SIZE              (4 * 3 + MAX_FILE_SYSTEM_TYPE_LENGTH + PATH_PATTERN_SIZE)

#define IP_ADDRESS_SIZE              16
#define MAX_PORTS_COUNT              16
#define NET_RULE_SIZE                (4 * 2 + 8 * 3 + 2 * (2 + MAX_PORTS_COUNT) + IP_ADDRESS_SIZE * 2)

/* Profile mode */
#define ENFORCE_MODE                 0x00000001U
#define COMPLAIN_MODE                0x00000002U

/* Rule mode */
#define DENY_MODE                    0x00000001U
#define AUDIT_MODE                   0x00000002U

/* Enforcement action */
#define DENIED_ACTION                0x00000001U
#define AUDIT_ACTION                 0x00000002U
#define ALLOWED_ACTION               0x00000004U

/* Matching flags — path rules */
#define PRECISE_MATCH                0x00000001U
#define GREEDY_MATCH                 0x00000002U
#define PREFIX_MATCH                 0x00000004U
#define SUFFIX_MATCH                 0x00000008U

/* Matching flags — network rules */
#define CIDR_MATCH                   0x00000020U
#define IPV4_MATCH                   0x00000040U
#define IPV6_MATCH                   0x00000080U
#define PORT_MATCH                   0x00000100U
#define SOCKET_MATCH                 0x00000200U
#define PORT_RANGE_MATCH             0x00000400U
#define PORTS_MATCH                  0x00000800U
#define POD_SELF_IP_MATCH            0x00001000U

/* File permissions (AppArmor-compatible) */
#define AA_MAY_EXEC                  0x00000001U
#define AA_MAY_WRITE                 0x00000002U
#define AA_MAY_READ                  0x00000004U
#define AA_MAY_APPEND                0x00000008U
#define AA_MAY_CREATE                0x00000010U
#define AA_MAY_RENAME                0x00000080U
#define AA_MAY_LINK                  0x00040000U

/* Ptrace permissions */
#define AA_PTRACE_TRACE              0x00000002U
#define AA_PTRACE_READ               0x00000004U
#define AA_MAY_BE_TRACED             0x00000008U
#define AA_MAY_BE_READ               0x00000010U

/* Mount flag */
#define AA_MAY_UMOUNT                0x00000200U

/* Event types */
#define CAPABILITY_TYPE              0x00000001U
#define FILE_TYPE                    0x00000002U
#define BPRM_TYPE                    0x00000004U
#define NETWORK_TYPE                 0x00000008U
#define PTRACE_TYPE                  0x00000010U
#define MOUNT_TYPE                   0x00000020U

/* Network event subtypes */
#define CONNECT_TYPE                 0x00000001U
#define SOCKET_TYPE                  0x00000002U

/* Event header size */
#define EVENT_HEADER_SIZE            24

/* Pin paths */
#define PIN_PATH                     "/sys/fs/bpf/varmor"
#define AUDIT_RINGBUF_PIN_PATH       "/sys/fs/bpf/varmor/v_audit_rb"

/* Aliases for BPF kernel side (shorter names in BPF context) */
#define OUTER_MAP_ENTRIES_MAX        110
#define FILE_INNER_MAP_ENTRIES_MAX   50
#define BPRM_INNER_MAP_ENTRIES_MAX   50
#define NET_INNER_MAP_ENTRIES_MAX    50
#define MOUNT_INNER_MAP_ENTRIES_MAX  50
#define PODS_PER_NODE_MAX            110
#define RING_BUFFER_MAX              (4096 * 256)
#define BUFFER_MAX                   (PATH_MAX * 3)
#define PATH_DEPTH_MAX               30
#define FILE_PATH_PATTERN_SIZE_MAX   64
#define FILE_SYSTEM_TYPE_MAX         16

#define POD_SELF_IP                  "pod-self"
#define UNSPECIFIED_ADDR             "unspecified"

/* ──────────── structs (must match BPF side layout) ──────────── */

typedef struct {
    uint32_t mode;
    uint32_t padding;
    uint64_t caps;
} CapabilityRule;

typedef struct {
    uint32_t mode;
    uint32_t permissions;
    uint32_t flags;
    uint8_t  prefix[MAX_FILE_PATH_PATTERN_LENGTH];
    uint8_t  suffix[MAX_FILE_PATH_PATTERN_LENGTH];
} PathRule;

typedef struct {
    uint32_t mode;
    uint32_t flags;
    uint64_t domains;
    uint64_t types;
    uint64_t protocols;
    uint8_t  address[IP_ADDRESS_SIZE];
    uint8_t  mask[IP_ADDRESS_SIZE];
    uint16_t port;
    uint16_t end_port;
    uint16_t ports[MAX_PORTS_COUNT];
} NetworkRule;

typedef struct {
    uint32_t mode;
    uint32_t permissions;
    uint32_t flags;
} PtraceRule;

typedef struct {
    uint32_t mode;
    uint32_t mount_flags;
    uint32_t reverse_mount_flags;
    uint32_t flags;
    uint8_t  prefix[MAX_FILE_PATH_PATTERN_LENGTH];
    uint8_t  suffix[MAX_FILE_PATH_PATTERN_LENGTH];
    uint8_t  fstype[MAX_FILE_SYSTEM_TYPE_LENGTH];
} MountRule;

typedef struct {
    uint32_t flags;
    uint8_t  ipv4[IP_ADDRESS_SIZE];
    uint8_t  ipv6[IP_ADDRESS_SIZE];
} PodIp;

/* ──────────── audit event structs ──────────── */

typedef struct {
    uint32_t action;
    uint32_t type;
    uint32_t mnt_ns;
    uint32_t tgid;
    uint64_t ktime;
} EventHeader;

typedef struct {
    uint32_t capability;
} CapabilityEvent;

typedef struct {
    uint32_t permissions;
    uint8_t  path[4096];
    uint8_t  padding[20];
} PathEvent;

typedef struct {
    uint32_t domain;
    uint32_t type;
    uint32_t protocol;
} NetworkSocket;

typedef struct {
    uint32_t sa_family;
    uint32_t sin_addr;
    uint8_t  sin6_addr[16];
    uint16_t port;
} NetworkSockAddr;

typedef struct {
    uint32_t        type;
    NetworkSocket   socket;
    NetworkSockAddr addr;
} NetworkEvent;

typedef struct {
    uint32_t permission;
    bool     external;
} PtraceEvent;

typedef struct {
    uint8_t  path[4096];
    uint8_t  type[16];
    uint32_t flags;
} MountEvent;

/* ──────────── name helpers ──────────── */

const char *varmor_enforcement_action_name(uint32_t action);
const char *varmor_event_type_name(uint32_t type);
const char *varmor_capability_name(uint32_t cap);
const char *varmor_path_permission_name(uint32_t perm);
const char *varmor_socket_domain_name(uint32_t domain);
const char *varmor_socket_type_name(uint32_t type);
const char *varmor_socket_protocol_name(uint32_t proto);
const char *varmor_ptrace_permission_name(uint32_t perm);
const char *varmor_mount_flag_name(uint32_t flag);
const char *varmor_mount_bind_flag_name(uint32_t flags);

/* ──────────── rule constructors ──────────── */

int varmor_new_capability_rule(uint32_t mode, uint64_t capabilities,
                               CapabilityRule *out, char *errbuf, size_t errbuf_len);
int varmor_new_path_rule(uint32_t mode, const char *pattern, uint32_t permissions,
                         PathRule *out, char *errbuf, size_t errbuf_len);
int varmor_new_network_connect_rule(uint32_t mode, const char *cidr,
                                    const char *ip_address, uint16_t port,
                                    uint16_t end_port, const uint16_t *ports,
                                    size_t ports_len, NetworkRule *out,
                                    char *errbuf, size_t errbuf_len);
int varmor_new_network_create_rule(uint32_t mode, uint64_t domains, uint64_t types,
                                   uint64_t protocols, NetworkRule *out,
                                   char *errbuf, size_t errbuf_len);
int varmor_new_ptrace_rule(uint32_t mode, uint32_t permissions, uint32_t flags,
                           PtraceRule *out);
int varmor_new_mount_rule(uint32_t mode, const char *source_pattern,
                          const char *fstype, uint32_t mount_flags,
                          uint32_t reverse_mount_flags, MountRule *out,
                          char *errbuf, size_t errbuf_len);

/* ──────────── enforcer API ──────────── */

#include "libbpf.h"
#include "bpf.h"

struct varmor_enforcer {
    struct bpf_object  *obj;
    struct bpf_program *prog_capable;
    struct bpf_program *prog_file_open;
    struct bpf_program *prog_path_symlink;
    struct bpf_program *prog_path_link;
    struct bpf_program *prog_path_rename;
    struct bpf_program *prog_bprm_check_security;
    struct bpf_program *prog_socket_connect;
    struct bpf_program *prog_socket_create;
    struct bpf_program *prog_ptrace_access_check;
    struct bpf_program *prog_mount;
    struct bpf_program *prog_move_mount;
    struct bpf_program *prog_umount;

    struct bpf_link *link_capable;
    struct bpf_link *link_file_open;
    struct bpf_link *link_path_symlink;
    struct bpf_link *link_path_link;
    struct bpf_link *link_path_rename;
    struct bpf_link *link_bprm;
    struct bpf_link *link_sock_conn;
    struct bpf_link *link_socket;
    struct bpf_link *link_ptrace;
    struct bpf_link *link_mount;
    struct bpf_link *link_move_mount;
    struct bpf_link *link_umount;

    int map_profile_mode_fd;
    int map_capable_fd;
    int map_file_outer_fd;
    int map_bprm_outer_fd;
    int map_net_outer_fd;
    int map_pod_ip_fd;
    int map_ptrace_fd;
    int map_mount_outer_fd;
    int map_audit_rb_fd;
};

typedef struct varmor_enforcer VarmorEnforcer;

VarmorEnforcer *varmor_enforcer_new(void);
void            varmor_enforcer_free(VarmorEnforcer *e);
int             varmor_enforcer_init(VarmorEnforcer *e, const char *bpf_object_path);
// Load BPF from embedded byte array (no external file needed)int varmor_enforcer_init_embedded(VarmorEnforcer *e, const void *data, size_t data_sz);
int             varmor_enforcer_remove(VarmorEnforcer *e);
int             varmor_enforcer_start(VarmorEnforcer *e);
void            varmor_enforcer_stop(VarmorEnforcer *e);

/* ──────────── map operations ──────────── */

int varmor_set_profile_mode(VarmorEnforcer *e, uint32_t mnt_ns_id,
                            uint32_t profile_mode);
int varmor_clear_profile_mode(VarmorEnforcer *e, uint32_t mnt_ns_id);

int varmor_set_capable(VarmorEnforcer *e, uint32_t mnt_ns_id,
                       const CapabilityRule *rule);
int varmor_clear_capable(VarmorEnforcer *e, uint32_t mnt_ns_id);

int varmor_set_file(VarmorEnforcer *e, uint32_t mnt_ns_id,
                    const PathRule *rule);
int varmor_clear_file(VarmorEnforcer *e, uint32_t mnt_ns_id);

int varmor_set_bprm(VarmorEnforcer *e, uint32_t mnt_ns_id,
                    const PathRule *rule);
int varmor_clear_bprm(VarmorEnforcer *e, uint32_t mnt_ns_id);

int varmor_set_network(VarmorEnforcer *e, uint32_t mnt_ns_id,
                       const NetworkRule *rules, size_t rule_count);
int varmor_clear_network(VarmorEnforcer *e, uint32_t mnt_ns_id);

int varmor_set_pod_ips(VarmorEnforcer *e, uint32_t mnt_ns_id,
                       const char *const *addresses, size_t address_count);
int varmor_remove_pod_ips(VarmorEnforcer *e, uint32_t mnt_ns_id);

int varmor_set_ptrace(VarmorEnforcer *e, uint32_t mnt_ns_id,
                      const PtraceRule *rule);
int varmor_clear_ptrace(VarmorEnforcer *e, uint32_t mnt_ns_id);

int varmor_set_mount(VarmorEnforcer *e, uint32_t mnt_ns_id,
                     const MountRule *rule);
int varmor_clear_mount(VarmorEnforcer *e, uint32_t mnt_ns_id);

int varmor_load_audit_ringbuf(int *ringbuf_fd);
int varmor_read_audit_events(int ringbuf_fd);

/* ──────────── utilities ──────────── */

int varmor_read_mnt_ns_id(uint32_t pid, uint32_t *mnt_ns_id,
                          char *errbuf, size_t errbuf_len);

#ifdef __cplusplus
}
#endif

#endif /* VARMOR_LSM_H */
// Low-level: create an inner hash map (for batching file/bprm rules)
int create_inner_hash_map(const char *name, uint32_t value_size, uint32_t max_entries);
