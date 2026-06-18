#include "vmlinux.h"
#include "bpf_helpers.h"
#include "bpf_tracing.h"
#include "bpf_core_read.h"
#include "bpf_endian.h"
// SPDX-License-Identifier: GPL-2.0
// Copyright 2023 vArmor-ebpf Authors

#ifndef __ENFORCER_H
#define __ENFORCER_H

#include "vmlinux.h"
#include "bpf_helpers.h"
#include "bpf_tracing.h"
#include "bpf_core_read.h"

// #define DEBUG 1
#ifdef DEBUG
#define DEBUG_PRINT(fmt, args...) \
  bpf_printk(fmt, ##args)
#else
#define DEBUG_PRINT(fmt, args...) \
  do { } while (0)
#endif

#undef container_of
#define container_of(ptr, type, member)                                        \
  ({                                                                           \
    const typeof(((type *)0)->member) *__mptr = (ptr);                         \
    (type *)((char *)__mptr - offsetof(type, member));                         \
  })

#define TO_MASK(x) (1ULL << x)

#define	EPERM 1
#define NAME_MAX      256
#define PATH_MAX      4096

// Profile mode
#define ENFORCE_MODE 0x00000001
#define COMPLAIN_MODE 0x00000002

// Rule mode
#define DENY_MODE   0x00000001
#define AUDIT_MODE  0x00000002

// Enforcement action
#define DENIED_ACTION  0x00000001
#define AUDIT_ACTION   0x00000002
#define ALLOWED_ACTION 0x00000004

// Maximum number of pods per node
#define PODS_PER_NODE_MAX 110

// Maximum containers count supported by BPF enforcer on a node.
#define OUTER_MAP_ENTRIES_MAX 110

// Maximum size of the per-CPU array buffer to cache paths and names etc.
#define BUFFER_MAX PATH_MAX*3

// Maximum size of the bpf ring buffer for auditing.
#define RING_BUFFER_MAX 4096*256

// Maximum extraction depth of the paths.
#define PATH_DEPTH_MAX 30

// Maximum size of the match pattern.
#define FILE_PATH_PATTERN_SIZE_MAX  64

// Maximum size of filesystem type
#define FILE_SYSTEM_TYPE_MAX 16

// Matching flags.
#define PRECISE_MATCH 0x00000001
#define GREEDY_MATCH  0x00000002
#define PREFIX_MATCH  0x00000004
#define SUFFIX_MATCH  0x00000008

// Matching flags for network rule
#define CIDR_MATCH        0x00000020
#define IPV4_MATCH        0x00000040
#define IPV6_MATCH        0x00000080
#define PORT_MATCH        0x00000100
#define SOCKET_MATCH      0x00000200
#define PORT_RANGE_MATCH  0x00000400
#define PORTS_MATCH       0x00000800
#define POD_SELF_IP_MATCH 0x00001000

// Event types
#define CAPABILITY_TYPE 0x00000001
#define FILE_TYPE       0x00000002
#define BPRM_TYPE       0x00000004
#define NETWORK_TYPE    0x00000008
#define PTRACE_TYPE     0x00000010
#define MOUNT_TYPE      0x00000020

// Event subtypes for network event
#define CONNETC_TYPE 0x00000001
#define SOCKET_TYPE  0x00000002

// v_profile_mode is a hash map to store the profile mode for the target.
// The key is the mount namespace ID, and the value is the profile mode.
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, u32);
	__type(value, u32);
	__uint(max_entries, OUTER_MAP_ENTRIES_MAX);
} v_profile_mode SEC(".maps");

static __always_inline u32 *get_profile_mode(u32 mnt_ns) {
    return bpf_map_lookup_elem(&v_profile_mode, &mnt_ns);
}

/*
  We use the buffer to cache file path and file name etc.
  |---------------------------------------|---------------------------------------|---------------------------------------|
  |                                       |                                       |                                       |
  |                              file path|                                       |file name                              | file_open()
  |                                 path-1|                                 path-2|name-1   |name-2                       | path_symlink(), path_link(), path_rename()
  |exec path                              |                                       |exec name|                             | bprm_check_security()
  |dev path                               |                                       |dev name |                    |fstype  | mount()
  |                              from path|                                       |from name|                             | move_mount()
  |                                       |                                       |                                       |
  |---------------------------------------|---------------------------------------|---------------------------------------|

  |------------------4096-----------------|------------------4096-----------------|---256---|---256---| |---16---|---16---|
*/
struct buffer {
  unsigned char value[BUFFER_MAX];
};

// buffer_offset cache the offset of the file path and file name in the buffer.
struct buffer_offset {
  u32 first_path;
  u32 first_name;
  u32 second_path;
  u32 second_name;
};

// v_buffer is a per-CPU array for the buffer.  
struct {
  __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
  __type(key, u32);
  __type(value, struct buffer);
  __uint(max_entries, 1);
} v_buffer SEC(".maps");

struct v_path {
  u32 permissions;
  unsigned char path[PATH_MAX];
};

struct v_socket {
  u32 domain;
  u32 type;
  u32 protocol;
};

struct v_sockaddr {
  u32 sa_family;
  u32 sin_addr;
  unsigned char sin6_addr[16];
  u16 port;
};

struct v_network {
  u32 type;
  struct v_socket socket;
  struct v_sockaddr addr;
};

struct v_ptrace {
  u32 permission;
  bool external;
};

struct v_mount {
  unsigned char path[PATH_MAX];
  unsigned char type[FILE_SYSTEM_TYPE_MAX];
  u32 flags;
};

// audit_event is the event structure for auditing and modeling.
struct audit_event {
  u32 action;
  u32 type;
  u32 mnt_ns;
  u32 tgid;
  u64 ktime;
  union {
    unsigned char buffer[4120];
    u32 capability;
    struct v_path path;
    struct v_network network;
    struct v_ptrace ptrace;
    struct v_mount mount;
  } event_u;
};

const struct audit_event *unused __attribute__((unused));

// v_audit_rb is a ring buffer to cache audit events
struct {
  __uint(type, BPF_MAP_TYPE_RINGBUF);
  __uint(max_entries, RING_BUFFER_MAX);
  __uint(pinning, LIBBPF_PIN_BY_NAME);
} v_audit_rb SEC(".maps");

struct path_pattern {
  u32 flags;
  unsigned char prefix[FILE_PATH_PATTERN_SIZE_MAX];
  unsigned char suffix[FILE_PATH_PATTERN_SIZE_MAX];
};

static struct buffer *get_buffer() {
    int index = 0;
    return bpf_map_lookup_elem(&v_buffer, &index);
}

static u32 get_task_mnt_ns_id(struct task_struct *task) {
  return BPF_CORE_READ(task, nsproxy, mnt_ns, ns).inum;
}

static struct user_namespace *get_task_user_ns(struct task_struct *task) {
  return BPF_CORE_READ(task, cred, user_ns);
}

static kernel_cap_t get_task_cap_effective(struct task_struct *task) {
  return BPF_CORE_READ(task, cred, cap_effective);
}

// static u32 get_task_uts_ns_id(struct task_struct *task) {
//   return BPF_CORE_READ(task, nsproxy, uts_ns, ns).inum;
// }

// static struct file *get_task_exe_file(struct task_struct *task) {
//   return BPF_CORE_READ(task, mm, exe_file);
// }

static int task_in_execve(struct task_struct *task) {
  unsigned long long val = 0;
  unsigned int offset = __builtin_preserve_field_info(task->in_execve, BPF_FIELD_BYTE_OFFSET);
  unsigned int size = __builtin_preserve_field_info(task->in_execve, BPF_FIELD_BYTE_SIZE);
  bpf_probe_read(&val, size, (void *)task + offset);
  val <<= __builtin_preserve_field_info(task->in_execve, BPF_FIELD_LSHIFT_U64);
  val >>= __builtin_preserve_field_info(task->in_execve, BPF_FIELD_RSHIFT_U64);
  return (int)val;
}

static inline struct mount *real_mount(struct vfsmount *mnt) {
  return container_of(mnt, struct mount, mnt);
}

// prepend_path_to_first_block - parse the file path to the first block but ignores chroot'ed root.
static __noinline int prepend_path_to_first_block(struct dentry *dentry, struct vfsmount *vfsmnt, struct buffer *buf, struct buffer_offset *buf_offset) {
  struct mount *mnt = real_mount(vfsmnt);
  struct mount *mnt_parent;
  struct dentry *dentry_parent;
  struct dentry *mnt_root;
  struct qstr d_name;

  char slash = '/';
  char null = '\0';
  int offset = PATH_MAX;

  #pragma unroll
  for (int i = 0; i < PATH_DEPTH_MAX; i++) {
    mnt_root = BPF_CORE_READ(mnt, mnt).mnt_root;
    if (mnt_root == dentry) {
      mnt_parent = BPF_CORE_READ(mnt, mnt_parent);
      if (mnt_parent != mnt) {
        dentry = BPF_CORE_READ(mnt, mnt_mountpoint);
        mnt = mnt_parent;
      }
    }

    dentry_parent = BPF_CORE_READ(dentry, d_parent);
    if (dentry_parent == dentry) {
      break;
    }

    d_name = BPF_CORE_READ(dentry, d_name);

    offset -= (d_name.len + 1);
    if (offset < 0)
      break;

    int ret = bpf_probe_read(
                  &(buf->value[offset & (PATH_MAX - 1)]),
                  d_name.len & (NAME_MAX - 1), 
                  d_name.name);
    if (ret == 0) {
      bpf_probe_read(
          &(buf->value[(offset + d_name.len) & (PATH_MAX - 1)]),
          1,
          &slash);

      // cache the file name to the 3nd block of buffer
      if (buf_offset->first_name == 0) {
        bpf_probe_read(
                  &(buf->value[PATH_MAX*2]),
                  d_name.len & (NAME_MAX - 1),
                  d_name.name);
        bpf_probe_read(&(buf->value[(PATH_MAX*2 + d_name.len) & (PATH_MAX*3 - 1)]), 1, &null);
        buf_offset->first_name = d_name.len;

      }
    } else {
      offset += (d_name.len + 1);
    }

    dentry = dentry_parent;
  }

  // the path must end with '\0'
  if (offset == PATH_MAX) {
    offset--;
  }
  bpf_probe_read(&(buf->value[PATH_MAX - 1]), 1, &null);

  // the path must start with '/'
  offset--;
  bpf_probe_read(&(buf->value[offset & (PATH_MAX - 1)]), 1, &slash);

  buf_offset->first_path = offset;
  return 0;
}

// prepend_path_to_second_block - parse the file path to the second block but ignores chroot'ed root.
static __noinline int prepend_path_to_second_block(struct dentry *dentry, struct vfsmount *vfsmnt, struct buffer *buf, struct buffer_offset *buf_offset) {
  struct mount *mnt = real_mount(vfsmnt);
  struct mount *mnt_parent;
  struct dentry *dentry_parent;
  struct dentry *mnt_root;
  struct qstr d_name;

  char slash = '/';
  char null = '\0';
  int offset = PATH_MAX*2;

  #pragma unroll
  for (int i = 0; i < PATH_DEPTH_MAX; i++) {
    mnt_root = BPF_CORE_READ(mnt, mnt).mnt_root;
    if (mnt_root == dentry) {
      mnt_parent = BPF_CORE_READ(mnt, mnt_parent);
      if (mnt_parent != mnt) {
        dentry = BPF_CORE_READ(mnt, mnt_mountpoint);
        mnt = mnt_parent;
      }
    }

    dentry_parent = BPF_CORE_READ(dentry, d_parent);
    if (dentry_parent == dentry ) {
      break;
    }

    d_name = BPF_CORE_READ(dentry, d_name);

    offset -= (d_name.len + 1);
    if (offset < 0)
      break;

    int ret = bpf_probe_read(
                  &(buf->value[offset & (PATH_MAX*2 - 1)]),
                  d_name.len & (NAME_MAX - 1), 
                  d_name.name);
    if (ret == 0) {
      bpf_probe_read(
          &(buf->value[(offset + d_name.len) & (PATH_MAX*2 - 1)]),
          1,
          &slash);

      // cache the file name to the 3nd block of buffer
      if (buf_offset->second_name == 0) {
        bpf_probe_read(
                  &(buf->value[(PATH_MAX*2 + NAME_MAX) & (PATH_MAX*3 - 1)]),
                  d_name.len & (NAME_MAX - 1),
                  d_name.name);

        bpf_probe_read(&(buf->value[(PATH_MAX*2 + NAME_MAX + d_name.len) & (PATH_MAX*3 - 1)]), 1, &null);
        buf_offset->second_name = d_name.len;
      }
    } else {
      offset += (d_name.len + 1);
    }

    dentry = dentry_parent;
  }

  // the path must end with '\0'
  if (offset == PATH_MAX*2) {
    offset--;
  }
  bpf_probe_read(&(buf->value[PATH_MAX*2 - 1]), 1, &null);

  // the path must start with '/'
  offset--;
  bpf_probe_read(&(buf->value[offset & (PATH_MAX*2 - 1)]), 1, &slash);

  buf_offset->second_path = offset;
  return 0;
}

// prepend_string_to_first_block - copy the string to the first block
static __noinline int prepend_string_to_first_block(const char *string, struct buffer *buf, struct buffer_offset *buf_offset) {
  int ret = bpf_probe_read_kernel_str(buf->value, PATH_MAX, string);
  if (ret >= 0) {
    buf_offset->first_path = ret;
  } else {
    return -1;
  }

  int index = 0;
  for (; index < NAME_MAX; index++) {
    if (buf->value[(buf_offset->first_path - 1 - index) & (PATH_MAX - 1)] == '/')
      break;
  }

  if (index != 0 && index != NAME_MAX) {
    ret = bpf_probe_read_kernel_str(&(buf->value[PATH_MAX*2]), NAME_MAX, &(buf->value[(buf_offset->first_path - 1 - index + 1) & (PATH_MAX - 1)]));
    if (ret > 0)
      buf_offset->first_name = ret - 1;
  }

  return 0;
}


static __noinline bool is_prefix_match(unsigned char *prefix, unsigned char *path) {
  for (int i = 0; i < FILE_PATH_PATTERN_SIZE_MAX; i++) {
    if (prefix[i] == '\0')
      break;

    if (prefix[i] != path[i])
      return false;
  }

  return true;
}

static __noinline bool is_suffix_match(unsigned char *suffix, unsigned char *path, int offset) {
  for (int i = 0; i < FILE_PATH_PATTERN_SIZE_MAX; i++) {
    if (suffix[i] == '\0')
      break;

    if (suffix[i] != path[(offset - i) & (PATH_MAX-1)])
      return false;
  }

  return true;
}

static __always_inline bool old_path_check(struct path_pattern *pattern, struct buffer *buf, struct buffer_offset *offset) {

  DEBUG_PRINT("old_path_check() - pattern flags: 0x%x", pattern->flags);

  bool match = true;
  if (pattern->flags & GREEDY_MATCH || pattern->flags & PRECISE_MATCH) {
    // precise match or greedy match for the globbing "**" with file path
    DEBUG_PRINT("old_path_check() - matching path");

    if (pattern->flags & PREFIX_MATCH) {
      DEBUG_PRINT("old_path_check() - pattern prefix: %s", pattern->prefix);
      if (is_prefix_match(pattern->prefix, &(buf->value[offset->first_path & (PATH_MAX - 1)]))) {
        match = true;
      } else {
        match = false;
      }
    }

    if ((pattern->flags & SUFFIX_MATCH) && match) {
      DEBUG_PRINT("old_path_check() - pattern suffix: %s", pattern->suffix);
      if (is_suffix_match(pattern->suffix, buf->value, PATH_MAX - 2)) {
        match = true;
      } else {
        match = false;
      }
    }
  } else {
    // non-greedy match for the globbing "*" with file name
    DEBUG_PRINT("old_path_check() - matching name");

    if (pattern->flags & PREFIX_MATCH) {
      DEBUG_PRINT("old_path_check() - pattern prefix: %s", pattern->prefix);
      if (is_prefix_match(pattern->prefix, &(buf->value[PATH_MAX * 2]))) {
        match = true;
      } else {
        match = false;
      }
    }

    if ((pattern->flags & SUFFIX_MATCH) && match) {
      DEBUG_PRINT("old_path_check() - pattern suffix: %s", pattern->suffix);
      if (is_suffix_match(pattern->suffix, buf->value + PATH_MAX*2, offset->first_name - 1)) {
        match = true;
      } else {
        match = false;
      }
    }
  }

  return match;
}

static __noinline bool new_path_check(struct path_pattern *pattern, struct buffer *buf, struct buffer_offset *offset) {

  DEBUG_PRINT("new_path_check() - pattern flags: 0x%x", pattern->flags);

  bool match = true;
  if (pattern->flags & GREEDY_MATCH || pattern->flags & PRECISE_MATCH) {
    // precise match or greedy match for the globbing "**" with file path
    DEBUG_PRINT("new_path_check() - matching path");

    if (pattern->flags & PREFIX_MATCH) {
      DEBUG_PRINT("new_path_check() - pattern prefix: %s", pattern->prefix);
      if (is_prefix_match(pattern->prefix, &(buf->value[offset->second_path & (PATH_MAX*2 - 1)]))) {
        match = true;
      } else {
        match = false;
      }
    }

    if ((pattern->flags & SUFFIX_MATCH) && match) {
      DEBUG_PRINT("new_path_check() - pattern suffix: %s", pattern->suffix);
      if (is_suffix_match(pattern->suffix, buf->value + PATH_MAX, PATH_MAX*2 - 2)) {
        match = true;
      } else {
        match = false;
      }
    }
  } else {
    // non-greedy match for the globbing "*" with file name
    DEBUG_PRINT("new_path_check() - matching name");

    if (pattern->flags & PREFIX_MATCH) {
      DEBUG_PRINT("new_path_check() - pattern prefix: %s", pattern->prefix);
      if (is_prefix_match(pattern->prefix, &(buf->value[PATH_MAX*2 + NAME_MAX]))) {
        match = true;
      } else {
        match = false;
      }
    }

    if ((pattern->flags & SUFFIX_MATCH) && match) {
      DEBUG_PRINT("new_path_check() - pattern suffix: %s", pattern->suffix);
      if (is_suffix_match(pattern->suffix, buf->value + PATH_MAX*2, NAME_MAX + offset->second_name - 1)) {
        match = true;
      } else {
        match = false;
      }
    }
  }

  return match;
}

static __noinline bool head_path_check(struct path_pattern *pattern, struct buffer *buf, struct buffer_offset *offset) {

  DEBUG_PRINT("head_path_check() - pattern flags: 0x%x", pattern->flags);

  bool match = true;
  if (pattern->flags & GREEDY_MATCH || pattern->flags & PRECISE_MATCH) {
    // precise match or greedy match for the globbing "**" with file path
    DEBUG_PRINT("head_path_check() - matching path");

    if (pattern->flags & PREFIX_MATCH) {
      DEBUG_PRINT("head_path_check() - pattern prefix: %s", pattern->prefix);
      if (is_prefix_match(pattern->prefix, buf->value)) {
        match = true;
      } else {
        match = false;
      }
    }

    if ((pattern->flags & SUFFIX_MATCH) && match) {
      DEBUG_PRINT("head_path_check() - pattern suffix: %s", pattern->suffix);
      if (is_suffix_match(pattern->suffix, buf->value, offset->first_path - 2)) {
        match = true;
      } else {
        match = false;
      }
    }
  } else {
    // non-greedy match for the globbing "*" with file name
    DEBUG_PRINT("head_path_check() - matching name");

    if (pattern->flags & PREFIX_MATCH) {
      DEBUG_PRINT("head_path_check() - pattern prefix: %s", pattern->prefix);
      if (is_prefix_match(pattern->prefix, &(buf->value[PATH_MAX * 2]))) {
        match = true;
      } else {
        match = false;
      }
    }

    if ((pattern->flags & SUFFIX_MATCH) && match) {
      DEBUG_PRINT("head_path_check() - pattern suffix: %s", pattern->suffix);
      if (is_suffix_match(pattern->suffix, buf->value + PATH_MAX*2, offset->first_name - 1)) {
        match = true;
      } else {
        match = false;
      }
    }
  }

  return match;
}

#endif// SPDX-License-Identifier: GPL-2.0

#ifndef __PERMS_H
#define __PERMS_H

// https://elixir.bootlin.com/linux/v5.10.178/source/tools/include/nolibc/nolibc.h#L446
/* fcntl / open */
#define O_RDONLY      0x00000000
#define O_WRONLY      0x00000001
#define O_RDWR        0x00000002
#define O_CREAT       0x00000040
#define O_EXCL        0x00000080
#define O_NOCTTY      0x00000100
#define O_TRUNC       0x00000200
#define O_APPEND      0x00000400
#define O_NONBLOCK    0x00000800
#define O_DIRECTORY   0x00010000

// https://elixir.bootlin.com/linux/v5.10.178/source/include/linux/fs.h#L95
#define MAY_EXEC		  0x00000001
#define MAY_WRITE		  0x00000002
#define MAY_READ		  0x00000004
#define MAY_APPEND	  0x00000008
#define MAY_ACCESS	  0x00000010
#define MAY_OPEN		  0x00000020
#define MAY_CHDIR		  0x00000040
#define FMODE_READ    0x00000001
#define FMODE_WRITE   0x00000002

// https://elixir.bootlin.com/linux/v5.10.178/source/security/apparmor/include/perms.h#L16
// https://elixir.bootlin.com/linux/v5.10.178/source/security/apparmor/include/net.h#L72
#define AA_MAY_EXEC       MAY_EXEC
#define AA_MAY_WRITE  	  MAY_WRITE
#define AA_MAY_READ       MAY_READ
#define AA_MAY_APPEND		  MAY_APPEND
#define AA_MAY_CREATE     0x00000010
#define AA_MAY_RENAME     0x00000080
#define AA_MAY_LINK		    0x00040000
#define AA_PTRACE_TRACE   MAY_WRITE
#define AA_PTRACE_READ    MAY_READ
#define AA_MAY_BE_TRACED  AA_MAY_APPEND
#define AA_MAY_BE_READ		AA_MAY_CREATE

// https://elixir.bootlin.com/linux/v5.10.178/source/include/linux/ptrace.h#L62
#define PTRACE_MODE_READ	  0x01
#define PTRACE_MODE_ATTACH	0x02

// Generic flags of mount syscall
// https://elixir.bootlin.com/linux/v5.19.17/source/include/uapi/linux/mount.h
#define MS_RDONLY	            1	        /* Mount read-only */
#define MS_NOSUID	            2	        /* Ignore suid and sgid bits */
#define MS_NODEV	            4	        /* Disallow access to device special files */
#define MS_NOEXEC	            8	        /* Disallow program execution */
#define MS_SYNCHRONOUS	      16	      /* Writes are synced at once */
#define MS_MANDLOCK	          64        /* Allow mandatory locks on an FS */
#define MS_DIRSYNC	          128       /* Directory modifications are synchronous */
#define MS_NOATIME	          1024	    /* Do not update access times. */
#define MS_NODIRATIME	        2048	    /* Do not update directory access times */
#define MS_SILENT	            32768
#define MS_RELATIME	          (1<<21)	  /* Update atime relative to mtime/ctime. */
#define MS_I_VERSION	        (1<<23)   /* Update inode I_version field */
#define MS_STRICTATIME	      (1<<24)   /* Always perform atime updates */

// Command flags of mount syscall
// https://elixir.bootlin.com/linux/v5.19.17/source/include/uapi/linux/mount.h
#define MS_REMOUNT	    32	      /* Alter flags of a mounted FS */
#define MS_BIND		      4096
#define MS_MOVE		      8192
#define MS_REC		      16384
#define MS_UNBINDABLE	  (1<<17)	  /* change to unbindable */
#define MS_PRIVATE	    (1<<18)	  /* change to private */
#define MS_SLAVE	      (1<<19)	  /* change to slave */
#define MS_SHARED	      (1<<20)	  /* change to shared */

// Custom mount-flags for umount()
#define AA_MAY_UMOUNT  512

#endif /* __PERMS_H */// SPDX-License-Identifier: GPL-2.0
// Copyright 2023 vArmor-ebpf Authors

#ifndef __CAPABILITY_H
#define __CAPABILITY_H

#include "vmlinux.h"
#include "bpf_helpers.h"
#include "bpf_tracing.h"
#include "bpf_core_read.h"

#define CAP_LAST_CAP 40

struct capability_rule {
  u32 mode;
	u32 padding;
  u64 caps;
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, u32);
	__type(value, struct capability_rule);
	__uint(max_entries, OUTER_MAP_ENTRIES_MAX);
} v_capable SEC(".maps");

static __always_inline struct capability_rule *get_capability_rules(u32 mnt_ns) {
    return bpf_map_lookup_elem(&v_capable, &mnt_ns);
}

#endif /* __CAPABILITY_H */// SPDX-License-Identifier: GPL-2.0
// Copyright 2023 vArmor-ebpf Authors

#ifndef __FILE_H
#define __FILE_H

#include "vmlinux.h"
#include "bpf_helpers.h"
#include "bpf_tracing.h"
#include "bpf_core_read.h"

// Maximum rule count for file access control
#define FILE_INNER_MAP_ENTRIES_MAX 50

typedef unsigned int fmode_t;

struct {
  __uint(type, BPF_MAP_TYPE_HASH_OF_MAPS);
  __uint(max_entries, OUTER_MAP_ENTRIES_MAX);
  __type(key, u32);
  __type(value, u32);
} v_file_outer SEC(".maps");

struct path_rule {
  u32 mode;
  u32 permissions;
  struct path_pattern pattern;
};

static u32 *get_file_inner_map(u32 mnt_ns) {
  return bpf_map_lookup_elem(&v_file_outer, &mnt_ns);
}

static struct path_rule *get_file_rule(u32 *vfile_inner, u32 rule_id) {
  return bpf_map_lookup_elem(vfile_inner, &rule_id);
}

/**
 * map_file_to_perms - map file flags to AppArmor permissions
 * @file: open file to map flags to AppArmor permissions
 *
 * Returns: apparmor permission set for the file
 */
static __noinline u32 map_file_to_perms(struct file *file) {
  u32 perms = 0;
  unsigned int flags = BPF_CORE_READ(file, f_flags);
  fmode_t mode = BPF_CORE_READ(file, f_mode);

  DEBUG_PRINT("map_file_to_perms() - flags: 0x%x", flags);
  DEBUG_PRINT("map_file_to_perms() - mode: 0x%x", mode);

  if (mode & FMODE_WRITE)
    perms |= MAY_WRITE;
  if (mode & FMODE_READ)
    perms |= MAY_READ;
  if ((flags & O_APPEND) && (perms & MAY_WRITE))
    perms = (perms & ~MAY_WRITE) | MAY_APPEND;
  /* trunc implies write permission */
  if (flags & O_TRUNC)
    perms |= MAY_WRITE;
  if (flags & O_CREAT)
    perms |= AA_MAY_CREATE;

  DEBUG_PRINT("map_file_to_perms() - perms: 0x%x", perms);
  return perms;
}

static __always_inline int iterate_file_inner_map_for_file(u32 *vfile_inner, struct buffer *buf, struct buffer_offset *offset, u32 requested_perms, u32 mnt_ns) {
  for(int inner_id=0; inner_id<FILE_INNER_MAP_ENTRIES_MAX; inner_id++) {
    // The key of the inner map must start from 0
    struct path_rule *rule = get_file_rule(vfile_inner, inner_id);
    if (rule == NULL) {
      DEBUG_PRINT("");
      DEBUG_PRINT("access allowed");
      return 0;
    }

    DEBUG_PRINT("---- rule id: %d ----", inner_id);
    DEBUG_PRINT("requested permissions: 0x%x, rule permissions: 0x%x", requested_perms, rule->permissions);

    // Permission check
    if (rule->permissions & requested_perms) {
      if (old_path_check(&rule->pattern, buf, offset)) {
        DEBUG_PRINT("");
        
        // Submit the audit event
        if (rule->mode & AUDIT_MODE) {
          struct audit_event *e;
          e = bpf_ringbuf_reserve(&v_audit_rb, sizeof(struct audit_event), 0);
          if (e) {
            DEBUG_PRINT("write audit event to ringbuf");
            e->action = rule->mode & DENY_MODE ? DENIED_ACTION : AUDIT_ACTION;
            e->type = FILE_TYPE;
            e->mnt_ns = mnt_ns;
            e->tgid = bpf_get_current_pid_tgid()>>32;
            e->ktime = bpf_ktime_get_boot_ns();
            e->event_u.path.permissions = requested_perms;
            bpf_probe_read_kernel_str(&e->event_u.path.path, PATH_MAX-offset->first_path & (PATH_MAX-1), &(buf->value[offset->first_path & (PATH_MAX-1)]));
            bpf_ringbuf_submit(e, 0);
          }
        }

        if (rule->mode & DENY_MODE) {
          DEBUG_PRINT("access denied");
          return -EPERM;
        }
      }
    }
  }

  DEBUG_PRINT("");
  DEBUG_PRINT("access allowed");
  return 0;
}

static __noinline int iterate_file_inner_map_for_path_pair(u32 *vfile_inner, struct buffer *buf, struct buffer_offset *offset, u32 requested_perms, u32 mnt_ns) {
  for(int inner_id=0; inner_id<FILE_INNER_MAP_ENTRIES_MAX; inner_id++) {
    // The key of the inner map must start from 0
    struct path_rule *rule = get_file_rule(vfile_inner, inner_id);
    if (rule == NULL) {
      DEBUG_PRINT("");
      DEBUG_PRINT("access allowed");
      return 0;
    }

    DEBUG_PRINT("---- rule id: %d ----", inner_id);
    DEBUG_PRINT("requested permissions: 0x%x, rule permissions: 0x%x", requested_perms, rule->permissions);

    // Permission check
    if (rule->permissions & AA_MAY_READ) {
      if (old_path_check(&rule->pattern, buf, offset)) {
        DEBUG_PRINT("");

        // Submit the audit event
        if (rule->mode & AUDIT_MODE) {
          struct audit_event *e;
          e = bpf_ringbuf_reserve(&v_audit_rb, sizeof(struct audit_event), 0);
          if (e) {
            DEBUG_PRINT("write audit event to ringbuf");
            e->action = rule->mode & DENY_MODE ? DENIED_ACTION : AUDIT_ACTION;
            e->type = FILE_TYPE;
            e->mnt_ns = mnt_ns;
            e->tgid = bpf_get_current_pid_tgid()>>32;
            e->ktime = bpf_ktime_get_boot_ns();
            e->event_u.path.permissions = requested_perms | AA_MAY_READ;
            bpf_probe_read_kernel_str(&e->event_u.path.path, PATH_MAX-offset->first_path & (PATH_MAX-1), &(buf->value[offset->first_path & (PATH_MAX-1)]));
            bpf_ringbuf_submit(e, 0);
          }
        }

        if (rule->mode & DENY_MODE) {
          DEBUG_PRINT("access denied");
          return -EPERM;
        }
      }
    }

    if (rule->permissions & AA_MAY_WRITE) {
      if (new_path_check(&rule->pattern, buf, offset)) {
        DEBUG_PRINT("");

        // Submit the audit event
        if (rule->mode & AUDIT_MODE) {
          struct audit_event *e;
          e = bpf_ringbuf_reserve(&v_audit_rb, sizeof(struct audit_event), 0);
          if (e) {
            DEBUG_PRINT("write audit event to ringbuf");
            e->action = rule->mode & DENY_MODE ? DENIED_ACTION : AUDIT_ACTION;
            e->type = FILE_TYPE;
            e->mnt_ns = mnt_ns;
            e->tgid = bpf_get_current_pid_tgid()>>32;
            e->ktime = bpf_ktime_get_boot_ns();
            e->event_u.path.permissions = requested_perms | AA_MAY_WRITE;
            bpf_probe_read_kernel_str(&e->event_u.path.path, PATH_MAX*2-offset->second_path & (PATH_MAX-1), &(buf->value[offset->second_path & (PATH_MAX*2-1)]));
            bpf_ringbuf_submit(e, 0);
          }
        }

        if (rule->mode & DENY_MODE) {
          DEBUG_PRINT("access denied");
          return -EPERM;
        }
      }
    }
  }

  DEBUG_PRINT("");
  DEBUG_PRINT("access allowed");
  return 0;
}

SEC("lsm/path_link")
int BPF_PROG(varmor_path_link_tail, struct dentry *old_dentry, const struct path *new_dir, struct dentry *new_dentry) {
  struct buffer *buf = get_buffer();
  if (buf == NULL)
    return 0;
  
  struct buffer_offset offset;
  bpf_probe_read(&offset, sizeof(offset), &buf->value[PATH_MAX*3-sizeof(offset)]);

  u32 mnt_ns;
  bpf_probe_read(&mnt_ns, 4, &buf->value[PATH_MAX*3-sizeof(offset)-4]);

  u32 *vfile_inner = get_file_inner_map(mnt_ns);
  if (vfile_inner == NULL) {
    return 0;
  }

  // Iterate all rules in the inner map
  return iterate_file_inner_map_for_path_pair(vfile_inner, buf, &offset, AA_MAY_LINK, mnt_ns);
}

SEC("lsm/path_rename")
int BPF_PROG(varmor_path_rename_tail, const struct path *old_dir, struct dentry *old_dentry, const struct path *new_dir, struct dentry *new_dentry, unsigned int flags) {
  struct buffer *buf = get_buffer();
  if (buf == NULL)
    return 0;
  
  struct buffer_offset offset;
  bpf_probe_read(&offset, sizeof(offset), &buf->value[PATH_MAX*3-sizeof(offset)]);

  u32 mnt_ns;
  bpf_probe_read(&mnt_ns, 4, &buf->value[PATH_MAX*3-sizeof(offset)-4]);

  u32 *vfile_inner = get_file_inner_map(mnt_ns);
  if (vfile_inner == NULL) {
    return 0;
  }

  // Iterate all rules in the inner map
  return iterate_file_inner_map_for_path_pair(vfile_inner, buf, &offset, AA_MAY_RENAME, mnt_ns);
}

#endif /* __FILE_H */// SPDX-License-Identifier: GPL-2.0
// Copyright 2023 vArmor-ebpf Authors

#ifndef __PROCESS_H
#define __PROCESS_H

#include "vmlinux.h"
#include "bpf_helpers.h"
#include "bpf_tracing.h"
#include "bpf_core_read.h"

// Maximum rule count for execution control
#define BPRM_INNER_MAP_ENTRIES_MAX 50

struct {
  __uint(type, BPF_MAP_TYPE_HASH_OF_MAPS);
  __uint(max_entries, OUTER_MAP_ENTRIES_MAX);
  __type(key, u32);
  __type(value, u32);
} v_bprm_outer SEC(".maps");

static u32 *get_bprm_inner_map(u32 mnt_ns) {
  return bpf_map_lookup_elem(&v_bprm_outer, &mnt_ns);
}

static struct path_rule *get_bprm_rule(u32 *vbprm_inner, u32 rule_id) {
  return bpf_map_lookup_elem(vbprm_inner, &rule_id);
}

static __noinline int iterate_bprm_inner_map_for_executable(u32 *vbprm_inner, struct buffer *buf, struct buffer_offset *offset, u32 mnt_ns) {
  for(int inner_id=0; inner_id<BPRM_INNER_MAP_ENTRIES_MAX; inner_id++) {
    // The key of the inner map must start from 0
    struct path_rule *rule = get_bprm_rule(vbprm_inner, inner_id);
    if (rule == NULL) {
      DEBUG_PRINT("");
      DEBUG_PRINT("access allowed");
      return 0;
    }

    DEBUG_PRINT("---- rule id: %d ----", inner_id);
    DEBUG_PRINT("rule permissions: 0x%x", rule->permissions);

    // Permission check
    if (head_path_check(&rule->pattern, buf, offset)) {
      DEBUG_PRINT("");

      // Submit the audit event
      if (rule->mode & AUDIT_MODE) {
        struct audit_event *e;
        e = bpf_ringbuf_reserve(&v_audit_rb, sizeof(struct audit_event), 0);
        if (e) {
          DEBUG_PRINT("write audit event to ringbuf");
          e->action = rule->mode & DENY_MODE ? DENIED_ACTION : AUDIT_ACTION;
          e->type = BPRM_TYPE;
          e->mnt_ns = mnt_ns;
          e->tgid = bpf_get_current_pid_tgid()>>32;
          e->ktime = bpf_ktime_get_boot_ns();
          e->event_u.path.permissions = AA_MAY_EXEC;
          bpf_probe_read_kernel_str(&e->event_u.path.path, offset->first_path & (PATH_MAX-1), &buf->value);
          bpf_ringbuf_submit(e, 0);
        }
      }

      if (rule->mode & DENY_MODE) {
        DEBUG_PRINT("access denied");
        return -EPERM;
      }
    }
  }

  DEBUG_PRINT("");
  DEBUG_PRINT("access allowed");
  return 0;
}

#endif /* __PROCESS_H */// SPDX-License-Identifier: GPL-2.0
// Copyright 2023 vArmor-ebpf Authors

#ifndef __NETWORK_H
#define __NETWORK_H

#include "vmlinux.h"
#include "bpf_helpers.h"
#include "bpf_tracing.h"
#include "bpf_core_read.h"
#include "bpf_endian.h"

// Maximum rule count for network access control
#define NET_INNER_MAP_ENTRIES_MAX 50
// Maximum port count for network address rule
#define PORTS_COUNT_MAX 16

#define AF_UNIX		1	  /* Unix domain sockets 		*/
#define AF_INET		2	  /* Internet IP Protocol 	*/
#define AF_INET6	10	/* IP version 6			*/

struct net_sockaddr {
  unsigned char address[16];
  unsigned char mask[16];
  u16 port;
  u16 end_port;
  u16 ports[PORTS_COUNT_MAX];
};

struct net_socket {
  u64 domains;
  u64 types;
  u64 protocols;
};

struct net_rule {
  u32 mode;
  u32 flags;
  struct net_socket socket;
  struct net_sockaddr addr;
};

struct {
  __uint(type, BPF_MAP_TYPE_HASH_OF_MAPS);
  __uint(max_entries, OUTER_MAP_ENTRIES_MAX);
  __type(key, u32);
  __type(value, u32);
} v_net_outer SEC(".maps");

// Pods may be allocated at most 1 value for each of IPv4 and IPv6
struct pod_ip {
  u32 flags;
	unsigned char ipv4[16];
  unsigned char ipv6[16];
};

// The map caches the pod ip of the container.
// It uses the mnt_ns id as the key.
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, u32);
	__type(value, struct pod_ip);
	__uint(max_entries, PODS_PER_NODE_MAX);
} v_pod_ip SEC(".maps");

static u32 *get_net_inner_map(u32 mnt_ns) {
  return bpf_map_lookup_elem(&v_net_outer, &mnt_ns);
}

static struct net_rule *get_net_rule(u32 *vnet_inner, u32 rule_id) {
  return bpf_map_lookup_elem(vnet_inner, &rule_id);
}

static struct pod_ip *get_pod_ip(u32 mnt_ns) {
  return bpf_map_lookup_elem(&v_pod_ip, &mnt_ns);
}

static __noinline int iterate_net_inner_map_for_socket_connect(u32 *vnet_inner, struct sockaddr *address, u32 mnt_ns) {
  u32 inner_id, ip, i;
  u16 port;
  bool match;

  for(inner_id=0; inner_id<NET_INNER_MAP_ENTRIES_MAX; inner_id++) {
    // The key of the inner map must start from 0
    struct net_rule *rule = get_net_rule(vnet_inner, inner_id);
    if (rule == NULL) {
      DEBUG_PRINT("");
      DEBUG_PRINT("access allowed");
      return 0;
    }

    if (!(rule->flags & (IPV4_MATCH|IPV6_MATCH|CIDR_MATCH|PRECISE_MATCH|POD_SELF_IP_MATCH|PORT_MATCH|PORT_RANGE_MATCH|PORTS_MATCH))) {
      continue;
    }

    DEBUG_PRINT("---- rule id: %d ----", inner_id);
    match = true;

    if ((address->sa_family == AF_INET) && (rule->flags & IPV4_MATCH)) {
      // IPv4
      struct sockaddr_in *addr4 = (struct sockaddr_in *) address;
      DEBUG_PRINT("IPv4 address: 0x%x", addr4->sin_addr.s_addr);
      DEBUG_PRINT("IPv4 port: %d", bpf_ntohs(addr4->sin_port));

      if (rule->flags & CIDR_MATCH) {
        for (i = 0; i < 4; i++) {
          ip = (addr4->sin_addr.s_addr >> (8 * i)) & 0xff;
          if ((ip & rule->addr.mask[i]) != rule->addr.address[i]) {
            match = false;
            break;
          }
        }
      } else if (rule->flags & PRECISE_MATCH) {
        for (i = 0; i < 4; i++) {
          ip = (addr4->sin_addr.s_addr >> (8 * i)) & 0xff;
          if (ip != rule->addr.address[i]) {
            match = false;
            break;
          }
        }
      } else if (rule->flags & POD_SELF_IP_MATCH) {
        struct pod_ip *podip = get_pod_ip(mnt_ns);
        if (podip == NULL || !(podip->flags & IPV4_MATCH)) {
          continue;
        }
        for (i = 0; i < 4; i++) {
          ip = (addr4->sin_addr.s_addr >> (8 * i)) & 0xff;
          if (ip != podip->ipv4[i]) {
            match = false;
            break;
          }
        }
      }

      port = bpf_ntohs(addr4->sin_port);
      if (match) {
        if ((rule->flags & PORT_MATCH) && (rule->addr.port != port)) {
          match = false;
        } else if ((rule->flags & PORT_RANGE_MATCH) && (rule->addr.port > port || rule->addr.end_port < port)) {
          match = false;
        } else if (rule->flags & PORTS_MATCH) {
          for (i = 0; i < PORTS_COUNT_MAX; i++) {
            if (rule->addr.ports[i] == port) {
              break;
            }
            if (rule->addr.ports[i] == 0 || i == PORTS_COUNT_MAX-1) {
              match = false;
              break;
            }
          }
        }
      }
      
      if (match) {
        DEBUG_PRINT("");

        // Submit the audit event
        if (rule->mode & AUDIT_MODE) {
          struct audit_event *e;
          e = bpf_ringbuf_reserve(&v_audit_rb, sizeof(struct audit_event), 0);
          if (e) {
            DEBUG_PRINT("write audit event to ringbuf");
            e->action = rule->mode & DENY_MODE ? DENIED_ACTION : AUDIT_ACTION;
            e->type = NETWORK_TYPE;
            e->mnt_ns = mnt_ns;
            e->tgid = bpf_get_current_pid_tgid()>>32;
            e->ktime = bpf_ktime_get_boot_ns();
            e->event_u.network.type = CONNETC_TYPE;
            e->event_u.network.addr.sa_family = AF_INET;
            e->event_u.network.addr.sin_addr = addr4->sin_addr.s_addr;
            e->event_u.network.addr.port = port;
            bpf_ringbuf_submit(e, 0);
          }
        }

        if (rule->mode & DENY_MODE) {
          DEBUG_PRINT("access denied");
          return -EPERM;
        }
      }
    } else if ((address->sa_family == AF_INET6) && (rule->flags & IPV6_MATCH)) {
      // IPv6
      struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *) address;
      struct in6_addr ip6addr = BPF_CORE_READ(addr6, sin6_addr);

      DEBUG_PRINT("IPv6 address: %d:%d", ip6addr.in6_u.u6_addr8[0], ip6addr.in6_u.u6_addr8[1]);
      DEBUG_PRINT("IPv6 address: %d:%d", ip6addr.in6_u.u6_addr8[2], ip6addr.in6_u.u6_addr8[3]);
      DEBUG_PRINT("IPv6 address: %d:%d", ip6addr.in6_u.u6_addr8[4], ip6addr.in6_u.u6_addr8[5]);
      DEBUG_PRINT("IPv6 address: %d:%d", ip6addr.in6_u.u6_addr8[6], ip6addr.in6_u.u6_addr8[7]);
      DEBUG_PRINT("IPv6 port: %d", bpf_ntohs(addr6->sin6_port));

      if (rule->flags & CIDR_MATCH) {
        for (i = 0; i < 16; i++) {
          ip = ip6addr.in6_u.u6_addr8[i];
          if ((ip & rule->addr.mask[i]) != rule->addr.address[i]) {
            match = false;
            break;
          }
        }
      } else if (rule->flags & PRECISE_MATCH) {
        for (i = 0; i < 16; i++) {
          ip = ip6addr.in6_u.u6_addr8[i];
          if (ip != rule->addr.address[i]) {
            match = false;
            break;
          }
        }
      } else if (rule->flags & POD_SELF_IP_MATCH) {
        struct pod_ip *podip = get_pod_ip(mnt_ns);
        if (podip == NULL || !(podip->flags & IPV6_MATCH)) {
          continue;
        }
        for (i = 0; i < 16; i++) {
          ip = ip6addr.in6_u.u6_addr8[i];
          if (ip != podip->ipv6[i]) {
            match = false;
            break;
          }
        }
      }

      port = bpf_ntohs(addr6->sin6_port);
      if (match) {
        if ((rule->flags & PORT_MATCH) && (rule->addr.port != port)) {
          match = false;
        } else if ((rule->flags & PORT_RANGE_MATCH) && (rule->addr.port > port || rule->addr.end_port < port)) {
          match = false;
        } else if (rule->flags & PORTS_MATCH) {
          for (i = 0; i < PORTS_COUNT_MAX; i++) {
            if (rule->addr.ports[i] == port) {
              break;
            }
            if (rule->addr.ports[i] == 0 || i == PORTS_COUNT_MAX-1) {
              match = false;
              break;
            }
          }
        }
      }

      if (match) {
        DEBUG_PRINT("");

        // Submit the audit event
        if (rule->mode & AUDIT_MODE) {
          struct audit_event *e;
          e = bpf_ringbuf_reserve(&v_audit_rb, sizeof(struct audit_event), 0);
          if (e) {
            DEBUG_PRINT("write audit event to ringbuf");
            e->action = rule->mode & DENY_MODE ? DENIED_ACTION : AUDIT_ACTION;
            e->type = NETWORK_TYPE;
            e->mnt_ns = mnt_ns;
            e->tgid = bpf_get_current_pid_tgid()>>32;
            e->ktime = bpf_ktime_get_boot_ns();
            e->event_u.network.type = CONNETC_TYPE;
            e->event_u.network.addr.sa_family = AF_INET6;
            bpf_probe_read_kernel(e->event_u.network.addr.sin6_addr, 16, &ip6addr.in6_u.u6_addr8);
            e->event_u.network.addr.port = port;
            bpf_ringbuf_submit(e, 0);
          }
        }

        if (rule->mode & DENY_MODE) {
          DEBUG_PRINT("access denied");
          return -EPERM;
        }
      }
    }
  }

  DEBUG_PRINT("");
  DEBUG_PRINT("access allowed");
  return 0;
}

static __noinline int iterate_net_inner_map_for_socket_create(u32 *vnet_inner, struct v_socket *s, u32 mnt_ns) {
  u32 inner_id;

  for(inner_id=0; inner_id<NET_INNER_MAP_ENTRIES_MAX; inner_id++) {
    // The key of the inner map must start from 0
    struct net_rule *rule = get_net_rule(vnet_inner, inner_id);
    if (rule == NULL) {
      DEBUG_PRINT("");
      DEBUG_PRINT("access allowed");
      return 0;
    }

    if (!(rule->flags & SOCKET_MATCH)) {
      continue;
    }

    DEBUG_PRINT("---- rule id: %d ----", inner_id);
    DEBUG_PRINT("rule domains: 0x%lx, requested domain mask: 0x%lx", rule->socket.domains, TO_MASK(s->domain));
    DEBUG_PRINT("rule types: 0x%lx, requested domain mask: 0x%lx", rule->socket.types, TO_MASK(s->type));
    DEBUG_PRINT("rule protocols: 0x%lx, requested domain mask: 0x%lx", rule->socket.protocols, TO_MASK(s->protocol));

    if (rule->socket.domains && !(rule->socket.domains & TO_MASK(s->domain))) {
      continue;
    }

    if (rule->socket.types && !(rule->socket.types & TO_MASK(s->type))) {
      continue;
    }

    if (rule->socket.protocols && !(rule->socket.protocols & TO_MASK(s->protocol))) {
      continue;
    }

    DEBUG_PRINT("");

    // Submit the audit event
    if (rule->mode & AUDIT_MODE) {
      struct audit_event *e;
      e = bpf_ringbuf_reserve(&v_audit_rb, sizeof(struct audit_event), 0);
      if (e) {
        DEBUG_PRINT("write audit event to ringbuf");
          e->action = rule->mode & DENY_MODE ? DENIED_ACTION : AUDIT_ACTION;
          e->type = NETWORK_TYPE;
          e->mnt_ns = mnt_ns;
          e->tgid = bpf_get_current_pid_tgid()>>32;
          e->ktime = bpf_ktime_get_boot_ns();
          e->event_u.network.type = SOCKET_TYPE;
          e->event_u.network.socket.domain = s->domain;
          e->event_u.network.socket.type = s->type;
          e->event_u.network.socket.protocol = s->protocol;
          bpf_ringbuf_submit(e, 0);
      }
    }

    if (rule->mode & DENY_MODE) {
      DEBUG_PRINT("access denied");
      return -EPERM;
    }
  }

  DEBUG_PRINT("");
  DEBUG_PRINT("access allowed");
  return 0;
}

#endif /* __NETWORK_H */// SPDX-License-Identifier: GPL-2.0
// Copyright 2023 vArmor-ebpf Authors

#ifndef __PTRACE_H
#define __PTRACE_H

#include "vmlinux.h"
#include "bpf_helpers.h"
#include "bpf_tracing.h"
#include "bpf_core_read.h"

struct ptrace_rule {
  u32 mode;
	u32 permissions;
  u32 flags;
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, u32);
	__type(value, struct ptrace_rule);
	__uint(max_entries, OUTER_MAP_ENTRIES_MAX);
} v_ptrace SEC(".maps");

static __always_inline struct ptrace_rule *get_ptrace_rule(u32 mnt_ns) {
    return bpf_map_lookup_elem(&v_ptrace, &mnt_ns);
}

static __always_inline bool ptrace_permission_check(u32 current_mnt_ns, u32 child_mnt_ns, struct ptrace_rule *rule, u32 request_permission) {
  DEBUG_PRINT("current task(mnt ns: %u) request the vArmor ptrace permission(0x%x) of child task(mnt ns: %u)", 
          current_mnt_ns, request_permission, child_mnt_ns);

  if (rule->permissions & request_permission) {
    // deny all tasks
    if (rule->flags & GREEDY_MATCH) {
      DEBUG_PRINT("access denied");
      return false;
    }

    // only deny tasks outside the container
    if (rule->flags & PRECISE_MATCH && current_mnt_ns != child_mnt_ns) {
      DEBUG_PRINT("access denied");
      return false;
    }
  }

  DEBUG_PRINT("access allowed");
  return true;
}

#endif /* __PTRACE_H */// SPDX-License-Identifier: GPL-2.0
// Copyright 2023 vArmor-ebpf Authors

#ifndef __MOUNT_H
#define __MOUNT_H

#include "vmlinux.h"
#include "bpf_helpers.h"
#include "bpf_tracing.h"
#include "bpf_core_read.h"

// Maximum rule count for network access control
#define MOUNT_INNER_MAP_ENTRIES_MAX 50

struct {
  __uint(type, BPF_MAP_TYPE_HASH_OF_MAPS);
  __uint(max_entries, OUTER_MAP_ENTRIES_MAX);
  __type(key, u32);
  __type(value, u32);
} v_mount_outer SEC(".maps");

struct mount_rule {
  u32 mode;
  u32 mount_flags;
  u32 reverse_mount_flags;
  struct path_pattern pattern;
  unsigned char fstype[FILE_SYSTEM_TYPE_MAX];
};

static u32 *get_mount_inner_map(u32 mnt_ns) {
  return bpf_map_lookup_elem(&v_mount_outer, &mnt_ns);
}

static struct mount_rule *get_mount_rule(u32 *vmount_inner, u32 rule_id) {
  return bpf_map_lookup_elem(vmount_inner, &rule_id);
}

static __noinline int prepend_fstype_to_third_block(const char *fstype, struct buffer *buf) {
  int ret = bpf_probe_read_kernel_str(&(buf->value[PATH_MAX*3-FILE_SYSTEM_TYPE_MAX]), FILE_SYSTEM_TYPE_MAX, fstype);
  if (ret < 0) {
    buf->value[PATH_MAX*3-FILE_SYSTEM_TYPE_MAX] = 0;
    return -1;
  }
  return 0;
}

static __noinline bool mount_fstype_check(unsigned char *rule_fstype, unsigned char *fstype) {
  DEBUG_PRINT("mount_fstype_check()");
  if (rule_fstype[0] == '*') {
    return true;
  }

  for (int i = 0; i < FILE_SYSTEM_TYPE_MAX; i++) {
    if (rule_fstype[i] == 0 && fstype[i] == 0)
      return true;

    if (rule_fstype[i] != fstype[i])
      break;
  }

  return false;
}

static __noinline int iterate_mount_inner_map(u32 *vmount_inner, unsigned long flags, struct buffer *buf, struct buffer_offset *offset, u32 mnt_ns) {
  for (int inner_id=0; inner_id<MOUNT_INNER_MAP_ENTRIES_MAX; inner_id++) {
    // The key of the inner map must start from 0
    struct mount_rule *rule = get_mount_rule(vmount_inner, inner_id);
    if (rule == NULL) {
      DEBUG_PRINT("");
      DEBUG_PRINT("access allowed");
      return 0;
    }

    DEBUG_PRINT("---- rule id: %d ----", inner_id);
    DEBUG_PRINT("rule mount_flags: 0x%x, reverse_mount_flags: 0x%x", rule->mount_flags, rule->reverse_mount_flags);
    DEBUG_PRINT("rule fstype: %s", rule->fstype);

    // Permission check
    if (flags & rule->mount_flags || (~flags) & rule->reverse_mount_flags) {
      if (mount_fstype_check(rule->fstype, &(buf->value[PATH_MAX*3-FILE_SYSTEM_TYPE_MAX])) && 
          head_path_check(&rule->pattern, buf, offset)) {
        DEBUG_PRINT("");

        // Submit the audit event
        if (rule->mode & AUDIT_MODE) {
          struct audit_event *e;
          e = bpf_ringbuf_reserve(&v_audit_rb, sizeof(struct audit_event), 0);
          if (e) {
            DEBUG_PRINT("write audit event to ringbuf");
            e->action = rule->mode & DENY_MODE ? DENIED_ACTION : AUDIT_ACTION;
            e->type = MOUNT_TYPE;
            e->mnt_ns = mnt_ns;
            e->tgid = bpf_get_current_pid_tgid()>>32;
            e->ktime = bpf_ktime_get_boot_ns();
            bpf_probe_read_kernel_str(e->event_u.mount.path, offset->first_path & (PATH_MAX-1), buf->value);
            bpf_probe_read_kernel_str(e->event_u.mount.type, FILE_SYSTEM_TYPE_MAX, &(buf->value[PATH_MAX*3-FILE_SYSTEM_TYPE_MAX]));
            e->event_u.mount.flags = flags;
            bpf_ringbuf_submit(e, 0);
          }
        }

        if (rule->mode & DENY_MODE) {
          DEBUG_PRINT("access denied");
          return -EPERM;
        }
      }
    }
  }

  DEBUG_PRINT("");
  DEBUG_PRINT("access allowed");
  return 0;
}

static __noinline int iterate_mount_inner_map_extra(u32 *vmount_inner, unsigned long flags, struct buffer *buf, struct buffer_offset *offset, u32 mnt_ns) {
  for (int inner_id=0; inner_id<MOUNT_INNER_MAP_ENTRIES_MAX; inner_id++) {
    // The key of the inner map must start from 0
    struct mount_rule *rule = get_mount_rule(vmount_inner, inner_id);
    if (rule == NULL) {
      DEBUG_PRINT("");
      DEBUG_PRINT("access allowed");
      return 0;
    }

    DEBUG_PRINT("---- rule id: %d ----", inner_id);
    DEBUG_PRINT("rule mount_flags: 0x%x, reverse_mount_flags: 0x%x", rule->mount_flags, rule->reverse_mount_flags);
    DEBUG_PRINT("rule fstype: %s", rule->fstype);

    // Permission check
    if (flags & rule->mount_flags) {
      if (mount_fstype_check(rule->fstype, &(buf->value[PATH_MAX*3-FILE_SYSTEM_TYPE_MAX])) && 
          old_path_check(&rule->pattern, buf, offset)) {
        DEBUG_PRINT("");

        // Submit the audit event
        if (rule->mode & AUDIT_MODE) {
          struct audit_event *e;
          e = bpf_ringbuf_reserve(&v_audit_rb, sizeof(struct audit_event), 0);
          if (e) {
            DEBUG_PRINT("write audit event to ringbuf");
            e->action = rule->mode & DENY_MODE ? DENIED_ACTION : AUDIT_ACTION;
            e->type = MOUNT_TYPE;
            e->mnt_ns = mnt_ns;
            e->tgid = bpf_get_current_pid_tgid()>>32;
            e->ktime = bpf_ktime_get_boot_ns();
            bpf_probe_read_kernel_str(e->event_u.mount.path, (PATH_MAX-offset->first_path) & (PATH_MAX-1), &(buf->value[offset->first_path & (PATH_MAX-1)]));
            bpf_probe_read_kernel_str(e->event_u.mount.type, FILE_SYSTEM_TYPE_MAX, &(buf->value[PATH_MAX*3-FILE_SYSTEM_TYPE_MAX]));
            e->event_u.mount.flags = flags;
            bpf_ringbuf_submit(e, 0);
          }
        }

        if (rule->mode & DENY_MODE) {
          DEBUG_PRINT("access denied");
          return -EPERM;
        }
      }
    }
  }

  DEBUG_PRINT("");
  DEBUG_PRINT("access allowed");
  return 0;
}

#endif /* __MOUNT_H */// SPDX-License-Identifier: GPL-2.0
// Copyright 2023 vArmor-ebpf Authors


char __license[] SEC("license") = "GPL";

// Save the mnt ns id of init task
volatile const u32 init_mnt_ns;

// Tail call map (program array) initialized with program pointers.
struct {
    __uint(type, BPF_MAP_TYPE_PROG_ARRAY);
    __type(key, u32);
    __type(value, u32);
    __uint(max_entries, 2);
} file_progs SEC(".maps");

SEC("lsm/capable")
int BPF_PROG(varmor_capable, const struct cred *cred, struct user_namespace *ns, int cap, unsigned int opts, int ret) {
  // Retrieve the current task
  struct task_struct *current = (struct task_struct *)bpf_get_current_task();
  u32 mnt_ns = get_task_mnt_ns_id(current);

  // Whether the current task is confined in a profile
  u32 *profile_mode = get_profile_mode(mnt_ns);
  if (profile_mode == 0)
    return ret;

  struct user_namespace *current_ns = get_task_user_ns(current);
  kernel_cap_t current_cap_effective = get_task_cap_effective(current);
  // To achieve compatibility with kernel 6.3+, we make use of type casting.
  u64 current_effective_mask = *(u64 *)&current_cap_effective;

  // When writing to the /tmp directory, overlayfs temporarily overrides the current task's cred to set 
  // the xattr in the trusted namespace using CAP_SYS_ADMIN. Therefore, we need to skip the capability 
  // check to maintain compatibility with it.
  if (current_ns == ns && current_effective_mask == 0x1fffeffffff) {
    return ret;
  }

  // Since v1.4.0, containerd will enable the cgroup namespace by default in the cgroup v2 environment.
  // At this point, using setns/nsenter to enter the container requires CAP_SYS_ADMIN in the container's 
  // user namespace. Therefore, we need to ignore the capability check when current_effective_mask is 
  // 0x1ffffffffff and the current task's user namespace is the same as the ns parameter of capable().
  // This will ensure that pods/exec can run normally.
  if (current_ns == ns && current_effective_mask == 0x1ffffffffff) {
    return ret;
  }

  DEBUG_PRINT("================ lsm/capable ================");
  u64 request_cap_mask = TO_MASK(cap);
  DEBUG_PRINT("task(mnt ns: %u) current_effective_mask: 0x%lx, request_cap_mask: 0x%lx", 
          mnt_ns, current_effective_mask, request_cap_mask);

  if (*profile_mode == COMPLAIN_MODE) {
    // Record the behavior to the ringbuf
    struct audit_event *e;
    e = bpf_ringbuf_reserve(&v_audit_rb, sizeof(struct audit_event), 0);
    if (e) {
      DEBUG_PRINT("write audit event to ringbuf");
      e->action = ALLOWED_ACTION;
      e->type = CAPABILITY_TYPE;
      e->mnt_ns = mnt_ns;
      e->tgid = bpf_get_current_pid_tgid()>>32;
      e->ktime = bpf_ktime_get_boot_ns();
      e->event_u.capability = cap;
      bpf_ringbuf_submit(e, 0);
    }
  } else {
    // Return directly if there is no capability rule for the current task
    struct capability_rule *rule = get_capability_rules(mnt_ns);
    if (rule == NULL)
      return ret;

    // Permission check
    if (rule->caps & request_cap_mask) {
      // Submit the audit event
      if (rule->mode & AUDIT_MODE) {
        struct audit_event *e;
        e = bpf_ringbuf_reserve(&v_audit_rb, sizeof(struct audit_event), 0);
        if (e) {
          DEBUG_PRINT("write audit event to ringbuf");
          e->action = rule->mode & DENY_MODE ? DENIED_ACTION : AUDIT_ACTION;
          e->type = CAPABILITY_TYPE;
          e->mnt_ns = mnt_ns;
          e->tgid = bpf_get_current_pid_tgid()>>32;
          e->ktime = bpf_ktime_get_boot_ns();
          e->event_u.capability = cap;
          bpf_ringbuf_submit(e, 0);
        }
      }

      if (rule->mode & DENY_MODE) {
        DEBUG_PRINT("task(mnt ns: %u) is not allowed to use capability: 0x%x", mnt_ns, cap);
        return -EPERM;
      }
    }
  }

  return ret;
}

SEC("lsm/file_open")
int BPF_PROG(varmor_file_open, struct file *file) {
  // Retrieve the current task and its mnt ns id
  struct task_struct *current = (struct task_struct *)bpf_get_current_task();
  u32 mnt_ns = get_task_mnt_ns_id(current);

  // Don't check permission here if called from execve()
  if(task_in_execve(current))
    return 0;

  // Return directly if the current task is unconfined
  u32 *profile_mode = get_profile_mode(mnt_ns);
  if (profile_mode == NULL)
    return 0;

  // Return directly if there are no file rules for the current task
  u32 *vfile_inner = get_file_inner_map(mnt_ns);
  if (*profile_mode == ENFORCE_MODE && vfile_inner == NULL)
    return 0;

  // Prepare buffer
  struct buffer_offset offset = { .first_path = 0, .first_name = 0, .second_path = 0, .second_name = 0 };
  struct buffer *buf = get_buffer();
  if (buf == NULL)
    return 0;
  
  // Extract the file path from the file structure provided by LSM Hook
  struct path f_path = BPF_CORE_READ(file, f_path);
  prepend_path_to_first_block(f_path.dentry, f_path.mnt, buf, &offset);

  DEBUG_PRINT("================ lsm/file_open ================");
  DEBUG_PRINT("path: %s", &(buf->value[offset.first_path & (PATH_MAX-1)]));
  DEBUG_PRINT("offset: %d, length: %d", offset.first_path, PATH_MAX-offset.first_path-1);
  DEBUG_PRINT("file name: %s, length: %d", &(buf->value[PATH_MAX*2]), offset.first_name);

  u32 requested_perms = map_file_to_perms(file);
  
  if (*profile_mode == COMPLAIN_MODE) {
    // Ignore the behavior when the cred of current task is overridden by overlayfs temporarily
    // Note: 
    // Please remind the users do not execute the 'kubectl exec' command during the behavior modeling.
    // Otherwise, some exceptional behaviors will be recorded.
    kernel_cap_t current_cap_effective = get_task_cap_effective(current);
    u64 current_effective_mask = *(u64 *)&current_cap_effective;
    if (current_effective_mask == 0x1fffeffffff) {
      DEBUG_PRINT("current_effective_mask is 0x1fffeffffff, ignore the behavior");
      return 0;
    }

    // Record the behavior to the ringbuf
    struct audit_event *e;
    e = bpf_ringbuf_reserve(&v_audit_rb, sizeof(struct audit_event), 0);
    if (e) {
      DEBUG_PRINT("write audit event to ringbuf");
      e->action = ALLOWED_ACTION;
      e->type = FILE_TYPE;
      e->mnt_ns = mnt_ns;
      e->tgid = bpf_get_current_pid_tgid()>>32;
      e->ktime = bpf_ktime_get_boot_ns();
      e->event_u.path.permissions = requested_perms;
      bpf_probe_read_kernel_str(&e->event_u.path.path, PATH_MAX-offset.first_path & (PATH_MAX-1), &(buf->value[offset.first_path & (PATH_MAX-1)]));
      bpf_ringbuf_submit(e, 0);
    }
    return 0;
  } else if (*profile_mode == ENFORCE_MODE && vfile_inner != NULL) {
    // Iterate all rules of the inner map
    return iterate_file_inner_map_for_file(vfile_inner, buf, &offset, requested_perms, mnt_ns);
  } else {
    return 0;
  }
}

SEC("lsm/path_symlink")
int BPF_PROG(varmor_path_symlink, const struct path *dir, struct dentry *dentry, const char *old_name) {
  // Retrieve the current task and its mnt ns id
  struct task_struct *current = (struct task_struct *)bpf_get_current_task();
  u32 mnt_ns = get_task_mnt_ns_id(current);

  // Return directly if the current task is unconfined
  u32 *profile_mode = get_profile_mode(mnt_ns);
  if (profile_mode == NULL)
    return 0;

  // Return directly if there are no file rules for the current task
  u32 *vfile_inner = get_file_inner_map(mnt_ns);
  if (*profile_mode == ENFORCE_MODE && vfile_inner == NULL)
    return 0;

  // Prepare buffer
  struct buffer_offset offset = { .first_path = 0, .first_name = 0, .second_path = 0, .second_name = 0 };
  struct buffer *buf = get_buffer();
  if (buf == NULL)
    return 0;

  // Extract the file path from the dentry provided by LSM Hook
  prepend_path_to_first_block(dentry, dir->mnt, buf, &offset);

  DEBUG_PRINT("================ lsm/path_symlink ================");
  DEBUG_PRINT("path: %s", &(buf->value[offset.first_path & (PATH_MAX-1)]));
  DEBUG_PRINT("offset: %d, length: %d", offset.first_path, PATH_MAX-offset.first_path-1);
  DEBUG_PRINT("file name: %s, length: %d", &(buf->value[PATH_MAX*2]), offset.first_name);

  if (*profile_mode == COMPLAIN_MODE) {
    // Record the behavior to the ringbuf
    struct audit_event *e;
    e = bpf_ringbuf_reserve(&v_audit_rb, sizeof(struct audit_event), 0);
    if (e) {
      DEBUG_PRINT("write audit event to ringbuf");
      e->action = ALLOWED_ACTION;
      e->type = FILE_TYPE;
      e->mnt_ns = mnt_ns;
      e->tgid = bpf_get_current_pid_tgid()>>32;
      e->ktime = bpf_ktime_get_boot_ns();
      e->event_u.path.permissions = AA_MAY_LINK | AA_MAY_WRITE;
      bpf_probe_read_kernel_str(&e->event_u.path.path, PATH_MAX-offset.first_path & (PATH_MAX-1), &(buf->value[offset.first_path & (PATH_MAX-1)]));
      bpf_ringbuf_submit(e, 0);
    }
    return 0;
  } else if (*profile_mode == ENFORCE_MODE && vfile_inner != NULL) {
    // Iterate all rules in the inner map
    return iterate_file_inner_map_for_file(vfile_inner, buf, &offset, AA_MAY_LINK | AA_MAY_WRITE, mnt_ns);
  } else {
    return 0;
  }
}

SEC("lsm/path_link")
int BPF_PROG(varmor_path_link, struct dentry *old_dentry, const struct path *new_dir, struct dentry *new_dentry) {
  // Retrieve the current task and its mnt ns id
  struct task_struct *current = (struct task_struct *)bpf_get_current_task();
  u32 mnt_ns = get_task_mnt_ns_id(current);

  // Return directly if the current task is unconfined
  u32 *profile_mode = get_profile_mode(mnt_ns);
  if (profile_mode == NULL)
    return 0;

  // Return directly if there are no file rules for the current task
  u32 *vfile_inner = get_file_inner_map(mnt_ns);
  if (*profile_mode == ENFORCE_MODE && vfile_inner == NULL)
    return 0;

  // Prepare buffer
  struct buffer_offset offset = { .first_path = 0, .first_name = 0, .second_path = 0, .second_name = 0 };
  struct buffer *buf = get_buffer();
  if (buf == NULL)
    return 0;
  
  // Extract the file path from the old dentry provided by LSM Hook
  prepend_path_to_first_block(old_dentry, new_dir->mnt, buf, &offset);

  // Extract the file path from the new dentry provided by LSM Hook
  prepend_path_to_second_block(new_dentry, new_dir->mnt, buf, &offset);

  // Save the offset and the mnt_ns
  bpf_probe_read(&buf->value[PATH_MAX*3-sizeof(offset)], sizeof(offset), &offset);
  bpf_probe_read(&buf->value[PATH_MAX*3-sizeof(offset)-4], 4, &mnt_ns);

  DEBUG_PRINT("================ lsm/path_link ================");
  DEBUG_PRINT("old path: %s", &(buf->value[offset.first_path & (PATH_MAX-1)]));
  DEBUG_PRINT("offset: %d, length: %d", offset.first_path, PATH_MAX-offset.first_path-1);
  DEBUG_PRINT("file name: %s, length: %d", &(buf->value[PATH_MAX*2]), offset.first_name);
  DEBUG_PRINT("new path: %s", &(buf->value[offset.second_path & (PATH_MAX*2-1)]));
  DEBUG_PRINT("offset: %d, length: %d", offset.second_path, PATH_MAX*2-offset.second_path-1);
  DEBUG_PRINT("file name: %s, length: %d", &(buf->value[PATH_MAX*2+NAME_MAX]), offset.second_name);
  
  if (*profile_mode == COMPLAIN_MODE) {
    // Record the behavior to the ringbuf
    struct audit_event *e;

    e = bpf_ringbuf_reserve(&v_audit_rb, sizeof(struct audit_event), 0);
    if (e) {
      DEBUG_PRINT("write audit event of old path to ringbuf");
      e->action = ALLOWED_ACTION;
      e->type = FILE_TYPE;
      e->mnt_ns = mnt_ns;
      e->tgid = bpf_get_current_pid_tgid()>>32;
      e->ktime = bpf_ktime_get_boot_ns();
      e->event_u.path.permissions = AA_MAY_LINK | AA_MAY_READ;
      bpf_probe_read_kernel_str(&e->event_u.path.path, PATH_MAX-offset.first_path & (PATH_MAX-1), &(buf->value[offset.first_path & (PATH_MAX-1)]));
      bpf_ringbuf_submit(e, 0);
    }

    e = bpf_ringbuf_reserve(&v_audit_rb, sizeof(struct audit_event), 0);
    if (e) {
      DEBUG_PRINT("write audit event of new path to ringbuf");
      e->action = ALLOWED_ACTION;
      e->type = FILE_TYPE;
      e->mnt_ns = mnt_ns;
      e->tgid = bpf_get_current_pid_tgid()>>32;
      e->ktime = bpf_ktime_get_boot_ns();
      e->event_u.path.permissions = AA_MAY_LINK | AA_MAY_WRITE;
      bpf_probe_read_kernel_str(&e->event_u.path.path, PATH_MAX*2-offset.second_path & (PATH_MAX-1), &(buf->value[offset.second_path & (PATH_MAX*2-1)]));
      bpf_ringbuf_submit(e, 0);
    }
    return 0;
  } else if (*profile_mode == ENFORCE_MODE) {
    // Tail call
    bpf_tail_call(ctx, &file_progs, 0);
    return 0;
  } else {
    return 0;
  }
}

SEC("lsm/path_rename")
int BPF_PROG(varmor_path_rename, const struct path *old_dir, struct dentry *old_dentry, const struct path *new_dir, struct dentry *new_dentry, unsigned int flags) {
  // Retrieve the current task and its mnt ns id
  struct task_struct *current = (struct task_struct *)bpf_get_current_task();
  u32 mnt_ns = get_task_mnt_ns_id(current);

  // Return directly if the current task is unconfined
  u32 *profile_mode = get_profile_mode(mnt_ns);
  if (profile_mode == NULL)
    return 0;

  // Return directly if there are no file rules for the current task
  u32 *vfile_inner = get_file_inner_map(mnt_ns);
  if (*profile_mode == ENFORCE_MODE && vfile_inner == NULL)
    return 0;

  // Prepare buffer
  struct buffer_offset offset = { .first_path = 0, .first_name = 0, .second_path = 0, .second_name = 0 };
  struct buffer *buf = get_buffer();
  if (buf == NULL)
    return 0;
  
  // Extract the file path of the old dentry provided by LSM Hook
  prepend_path_to_first_block(old_dentry, old_dir->mnt, buf, &offset);

  // Extract the file path of the new dentry provided by LSM Hook
  prepend_path_to_second_block(new_dentry, new_dir->mnt, buf, &offset);

  // Save the offset and the mnt_ns
  bpf_probe_read(&buf->value[PATH_MAX*3-sizeof(offset)], sizeof(offset), &offset);
  bpf_probe_read(&buf->value[PATH_MAX*3-sizeof(offset)-4], 4, &mnt_ns);

  DEBUG_PRINT("================ lsm/path_rename ================");
  DEBUG_PRINT("old path: %s", &(buf->value[offset.first_path & (PATH_MAX-1)]));
  DEBUG_PRINT("offset: %d, length: %d", offset.first_path, PATH_MAX-offset.first_path-1);
  DEBUG_PRINT("file name: %s, length: %d", &(buf->value[PATH_MAX*2]), offset.first_name);
  DEBUG_PRINT("new path: %s", &(buf->value[offset.second_path & (PATH_MAX*2-1)]));
  DEBUG_PRINT("offset: %d, length: %d", offset.second_path, PATH_MAX*2-offset.second_path-1);
  DEBUG_PRINT("file name: %s, length: %d", &(buf->value[PATH_MAX*2+NAME_MAX]), offset.second_name);

  if (*profile_mode == COMPLAIN_MODE) {
    // Record the behavior to the ringbuf
    struct audit_event *e;

    e = bpf_ringbuf_reserve(&v_audit_rb, sizeof(struct audit_event), 0);
    if (e) {
      DEBUG_PRINT("write audit event of old path to ringbuf");
      e->action = ALLOWED_ACTION;
      e->type = FILE_TYPE;
      e->mnt_ns = mnt_ns;
      e->tgid = bpf_get_current_pid_tgid()>>32;
      e->ktime = bpf_ktime_get_boot_ns();
      e->event_u.path.permissions = AA_MAY_RENAME | AA_MAY_READ;
      bpf_probe_read_kernel_str(&e->event_u.path.path, PATH_MAX-offset.first_path & (PATH_MAX-1), &(buf->value[offset.first_path & (PATH_MAX-1)]));
      bpf_ringbuf_submit(e, 0);
    }

    e = bpf_ringbuf_reserve(&v_audit_rb, sizeof(struct audit_event), 0);
    if (e) {
      DEBUG_PRINT("write audit event of new path to ringbuf");
      e->action = ALLOWED_ACTION;
      e->type = FILE_TYPE;
      e->mnt_ns = mnt_ns;
      e->tgid = bpf_get_current_pid_tgid()>>32;
      e->ktime = bpf_ktime_get_boot_ns();
      e->event_u.path.permissions = AA_MAY_RENAME | AA_MAY_WRITE;
      bpf_probe_read_kernel_str(&e->event_u.path.path, PATH_MAX*2-offset.second_path & (PATH_MAX-1), &(buf->value[offset.second_path & (PATH_MAX*2-1)]));
      bpf_ringbuf_submit(e, 0);
    }
    return 0;
  } else if (*profile_mode == ENFORCE_MODE) {
    // Tail call
    bpf_tail_call(ctx, &file_progs, 1);
    return 0;
  } else {
    return 0;
  }
}

SEC("lsm/bprm_check_security")
int BPF_PROG(varmor_bprm_check_security, struct linux_binprm *bprm, int ret) {
  // Retrieve the current task and its mnt ns id
  struct task_struct *current = (struct task_struct *)bpf_get_current_task();
  u32 mnt_ns = get_task_mnt_ns_id(current);

  // Return directly if the current task is unconfined
  u32 *profile_mode = get_profile_mode(mnt_ns);
  if (profile_mode == NULL)
    return 0;

  // Return directly if there are no process rules for the current task
  u32 *vbprm_inner = get_bprm_inner_map(mnt_ns);
  if (*profile_mode == ENFORCE_MODE && vbprm_inner == NULL)
    return 0;

  // Prepare buffer
  struct buffer_offset offset = { .first_path = 0, .first_name = 0, .second_path = 0, .second_name = 0 };
  struct buffer *buf = get_buffer();
  if (buf == NULL)
    return 0;

  // Extract the new executable path from the bprm parameter
  prepend_string_to_first_block(bprm->filename, buf, &offset);

  DEBUG_PRINT("================ lsm/bprm_check_security ================");
  DEBUG_PRINT("path: %s", buf->value);
  DEBUG_PRINT("offset: %d, length: %d", offset.first_path, offset.first_path-1);
  DEBUG_PRINT("file name: %s, length: %d", &(buf->value[PATH_MAX*2]), offset.first_name);

  if (*profile_mode == COMPLAIN_MODE) {
    // Record the behavior to the ringbuf
    struct audit_event *e;
    e = bpf_ringbuf_reserve(&v_audit_rb, sizeof(struct audit_event), 0);
    if (e) {
      DEBUG_PRINT("write audit event to ringbuf");
      e->action = ALLOWED_ACTION;
      e->type = BPRM_TYPE;
      e->mnt_ns = mnt_ns;
      e->tgid = bpf_get_current_pid_tgid()>>32;
      e->ktime = bpf_ktime_get_boot_ns();
      e->event_u.path.permissions = AA_MAY_EXEC;
      bpf_probe_read_kernel_str(&e->event_u.path.path, offset.first_path & (PATH_MAX-1), &buf->value);
      bpf_ringbuf_submit(e, 0);
    }
    return 0;
  } else if (*profile_mode == ENFORCE_MODE && vbprm_inner != NULL) {
    // Iterate all rules of the inner map
    return iterate_bprm_inner_map_for_executable(vbprm_inner, buf, &offset, mnt_ns);
  } else {
    return 0;
  }
}

SEC("lsm/socket_connect")
int BPF_PROG(varmor_socket_connect, struct socket *sock, struct sockaddr *address, int addrlen) {
  // We only care about ipv4 and ipv6 for now
	if (address->sa_family != AF_INET && address->sa_family != AF_INET6)
		return 0;

  // Retrieve the current task and its mnt ns id
  struct task_struct *current = (struct task_struct *)bpf_get_current_task();
  u32 mnt_ns = get_task_mnt_ns_id(current);

  // Return directly if the current task is unconfined
  u32 *profile_mode = get_profile_mode(mnt_ns);
  if (profile_mode == NULL)
    return 0;

  DEBUG_PRINT("================ lsm/socket_connect ================");
  DEBUG_PRINT("socket status: 0x%x", sock->state);
  DEBUG_PRINT("socket type: 0x%x", sock->type);
  DEBUG_PRINT("socket flags: 0x%x", sock->flags);

  if (*profile_mode == COMPLAIN_MODE) {
    // Record the behavior to the ringbuf
    struct audit_event *e;

    if (address->sa_family == AF_INET) {
      // IPv4
      struct sockaddr_in *addr4 = (struct sockaddr_in *) address;

      DEBUG_PRINT("IPv4 address: 0x%x", addr4->sin_addr.s_addr);
      DEBUG_PRINT("IPv4 port: %d", bpf_ntohs(addr4->sin_port));
      DEBUG_PRINT("write audit event to ringbuf");
      e = bpf_ringbuf_reserve(&v_audit_rb, sizeof(struct audit_event), 0);
      if (e) {
        e->action = ALLOWED_ACTION;
        e->type = NETWORK_TYPE;
        e->mnt_ns = mnt_ns;
        e->tgid = bpf_get_current_pid_tgid()>>32;
        e->ktime = bpf_ktime_get_boot_ns();
        e->event_u.network.type = CONNETC_TYPE;
        e->event_u.network.addr.sa_family = AF_INET;
        e->event_u.network.addr.sin_addr = addr4->sin_addr.s_addr;
        e->event_u.network.addr.port = bpf_ntohs(addr4->sin_port);
        bpf_ringbuf_submit(e, 0);
      }
    } else if (address->sa_family == AF_INET6) {
      // IPv6
      struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *) address;
      struct in6_addr ip6addr = BPF_CORE_READ(addr6, sin6_addr);

      DEBUG_PRINT("IPv6 address: %d:%d", ip6addr.in6_u.u6_addr8[0], ip6addr.in6_u.u6_addr8[1]);
      DEBUG_PRINT("IPv6 address: %d:%d", ip6addr.in6_u.u6_addr8[2], ip6addr.in6_u.u6_addr8[3]);
      DEBUG_PRINT("IPv6 address: %d:%d", ip6addr.in6_u.u6_addr8[4], ip6addr.in6_u.u6_addr8[5]);
      DEBUG_PRINT("IPv6 address: %d:%d", ip6addr.in6_u.u6_addr8[6], ip6addr.in6_u.u6_addr8[7]);
      DEBUG_PRINT("IPv6 port: %d", bpf_ntohs(addr6->sin6_port));
      DEBUG_PRINT("write audit event to ringbuf");
      e = bpf_ringbuf_reserve(&v_audit_rb, sizeof(struct audit_event), 0);
      if (e) {
        e->action = ALLOWED_ACTION;
        e->type = NETWORK_TYPE;
        e->mnt_ns = mnt_ns;
        e->tgid = bpf_get_current_pid_tgid()>>32;
        e->ktime = bpf_ktime_get_boot_ns();
        e->event_u.network.type = CONNETC_TYPE;
        e->event_u.network.addr.sa_family = AF_INET6;
        bpf_probe_read_kernel(e->event_u.network.addr.sin6_addr, 16, &ip6addr.in6_u.u6_addr8);
        e->event_u.network.addr.port = bpf_ntohs(addr6->sin6_port);
        bpf_ringbuf_submit(e, 0);
      }
    }
    return 0;
  } else if (*profile_mode == ENFORCE_MODE) {
    // Return directly if there are no network rules for the current task
    u32 *vnet_inner = get_net_inner_map(mnt_ns);
    if (vnet_inner == NULL)
      return 0;

    // Iterate all rules in the inner map
    return iterate_net_inner_map_for_socket_connect(vnet_inner, address, mnt_ns);
  } else {
    return 0;
  }
}

SEC("lsm/socket_create")
int BPF_PROG(varmor_socket_create, int family, int type, int protocol, int kern) {
  // Ignore kernel socket
  if (kern == 1)
    return 0;

  // Retrieve the current task and its mnt ns id
  struct task_struct *current = (struct task_struct *)bpf_get_current_task();
  u32 mnt_ns = get_task_mnt_ns_id(current);

  // Return directly if the current task is unconfined
  u32 *profile_mode = get_profile_mode(mnt_ns);
  if (profile_mode == NULL)
    return 0;

  DEBUG_PRINT("================ lsm/socket_create ================");
  DEBUG_PRINT("socket family: 0x%x", family);
  DEBUG_PRINT("socket type: 0x%x", type);
  DEBUG_PRINT("socket protocol: 0x%x", protocol);

  if (*profile_mode == COMPLAIN_MODE) {
    // Record the behavior to the ringbuf
    struct audit_event *e;
    e = bpf_ringbuf_reserve(&v_audit_rb, sizeof(struct audit_event), 0);
    if (e) {
      DEBUG_PRINT("write audit event to ringbuf");
        e->action = ALLOWED_ACTION;
        e->type = NETWORK_TYPE;
        e->mnt_ns = mnt_ns;
        e->tgid = bpf_get_current_pid_tgid()>>32;
        e->ktime = bpf_ktime_get_boot_ns();
        e->event_u.network.type = SOCKET_TYPE;
        e->event_u.network.socket.domain = family;
        e->event_u.network.socket.type = type;
        e->event_u.network.socket.protocol = protocol;
        bpf_ringbuf_submit(e, 0);
    }
    return 0;
  } else if (*profile_mode == ENFORCE_MODE) {
    // Return directly if there are no network rules for the current task
    u32 *vnet_inner = get_net_inner_map(mnt_ns);
    if (vnet_inner == NULL)
      return 0;

    // Iterate all rules in the inner map
    struct v_socket s = { .domain = family, .type = type, .protocol = protocol};
    return iterate_net_inner_map_for_socket_create(vnet_inner, &s, mnt_ns);
  } else {
    return 0;
  }
}

SEC("lsm/ptrace_access_check")
int BPF_PROG(varmor_ptrace_access_check, struct task_struct *child, unsigned int mode) {
  // Retrieve the mnt ns id of the current task and the child task
  struct task_struct *current = (struct task_struct *)bpf_get_current_task();
  u32 current_mnt_ns = get_task_mnt_ns_id(current);
  u32 child_mnt_ns = get_task_mnt_ns_id(child);

  // Retrieve the profile mode of the current task and the child task
  u32 *current_profile_mode = get_profile_mode(current_mnt_ns);
  u32 *child_profile_mode = get_profile_mode(child_mnt_ns);

  if (current_profile_mode != NULL) {
    DEBUG_PRINT("================ lsm/ptrace_access_check/current_task ================");
    if (*current_profile_mode == COMPLAIN_MODE) {
      // Record the behavior to the ringbuf
      struct audit_event *e;
      e = bpf_ringbuf_reserve(&v_audit_rb, sizeof(struct audit_event), 0);
      if (e) {
        DEBUG_PRINT("write audit event to ringbuf");
        e->action = ALLOWED_ACTION;
        e->type = PTRACE_TYPE;
        e->mnt_ns = current_mnt_ns;
        e->tgid = bpf_get_current_pid_tgid()>>32;
        e->ktime = bpf_ktime_get_boot_ns();
        e->event_u.ptrace.permission = (mode & PTRACE_MODE_READ) ? AA_PTRACE_READ : AA_PTRACE_TRACE;
        e->event_u.ptrace.external = (current_mnt_ns != child_mnt_ns);
        bpf_ringbuf_submit(e, 0);
      }
    } else if (*current_profile_mode == ENFORCE_MODE) {
      // Retrieve the ptrace rule for the current task
      struct ptrace_rule *rule = get_ptrace_rule(current_mnt_ns);
      if (rule != NULL) {
        // Check whether the current task is allowed to trace or read a child task
        if (!ptrace_permission_check(current_mnt_ns, child_mnt_ns, rule, (mode & PTRACE_MODE_READ) ? AA_PTRACE_READ : AA_PTRACE_TRACE)) {
          // Submit the audit event
          if (rule->mode & AUDIT_MODE) {
            struct audit_event *e;
            e = bpf_ringbuf_reserve(&v_audit_rb, sizeof(struct audit_event), 0);
            if (e) {
              DEBUG_PRINT("write audit event to ringbuf");
              e->action = rule->mode & DENY_MODE ? DENIED_ACTION : AUDIT_ACTION;
              e->type = PTRACE_TYPE;
              e->mnt_ns = current_mnt_ns;
              e->tgid = bpf_get_current_pid_tgid()>>32;
              e->ktime = bpf_ktime_get_boot_ns();
              e->event_u.ptrace.permission = (mode & PTRACE_MODE_READ) ? AA_PTRACE_READ : AA_PTRACE_TRACE;
              e->event_u.ptrace.external = (current_mnt_ns != child_mnt_ns);
              bpf_ringbuf_submit(e, 0);
            }
          }

          if (rule->mode & DENY_MODE) {
            DEBUG_PRINT("current task(mnt ns: %u) is not allowed to read/trace the task(mnt ns: %u)", current_mnt_ns, child_mnt_ns);
            return -EPERM;
          }
        }
      }
    }
  }

  // By default, we allow the child task to be traced or read by tasks in the init mnt ns
  if (child_profile_mode != NULL && current_mnt_ns != init_mnt_ns) {
    DEBUG_PRINT("================ lsm/ptrace_access_check/child_task ================");
    if (*child_profile_mode == COMPLAIN_MODE) {
      // Record the behavior to the ringbuf
      struct audit_event *e;
      e = bpf_ringbuf_reserve(&v_audit_rb, sizeof(struct audit_event), 0);
      if (e) {
        DEBUG_PRINT("write audit event to ringbuf");
        e->action = ALLOWED_ACTION;
        e->type = PTRACE_TYPE;
        e->mnt_ns = child_mnt_ns;
        e->tgid = bpf_get_current_pid_tgid()>>32;
        e->ktime = bpf_ktime_get_boot_ns();
        e->event_u.ptrace.permission = (mode & PTRACE_MODE_READ) ? AA_MAY_BE_READ : AA_MAY_BE_TRACED;
        e->event_u.ptrace.external = (current_mnt_ns != child_mnt_ns);
        bpf_ringbuf_submit(e, 0);
      }
    } else if (*child_profile_mode == ENFORCE_MODE) {
      // Retrieve the ptrace rule for the child task
      struct ptrace_rule *rule = get_ptrace_rule(child_mnt_ns);
      if (rule != NULL) {
        // Check whether the child task is allowed to be traced or read by the current task
        if (!ptrace_permission_check(current_mnt_ns, child_mnt_ns, rule, (mode & PTRACE_MODE_READ) ? AA_MAY_BE_READ : AA_MAY_BE_TRACED)) {
          // Submit the audit event
          if (rule->mode & AUDIT_MODE) {
            struct audit_event *e;
            e = bpf_ringbuf_reserve(&v_audit_rb, sizeof(struct audit_event), 0);
            if (e) {
              DEBUG_PRINT("write audit event to ringbuf");
              e->action = rule->mode & DENY_MODE ? DENIED_ACTION : AUDIT_ACTION;
              e->type = PTRACE_TYPE;
              e->mnt_ns = child_mnt_ns;
              e->tgid = bpf_get_current_pid_tgid()>>32;
              e->ktime = bpf_ktime_get_boot_ns();
              e->event_u.ptrace.permission = (mode & PTRACE_MODE_READ) ? AA_MAY_BE_READ : AA_MAY_BE_TRACED;
              e->event_u.ptrace.external = (current_mnt_ns != child_mnt_ns);
              bpf_ringbuf_submit(e, 0);
            }
          }

          if (rule->mode & DENY_MODE) {
            DEBUG_PRINT("current task(mnt ns: %u) is not allowed to readby/traceby the task(mnt ns: %u)", current_mnt_ns, child_mnt_ns);
            return -EPERM;
          }
        }
      }
    }
  }

  return 0;
}

SEC("lsm/sb_mount")
int BPF_PROG(varmor_mount, char *dev_name, struct path *path, char *type, unsigned long flags, void *data) {
  // Retrieve the current task and its mnt ns id
  struct task_struct *current = (struct task_struct *)bpf_get_current_task();
  u32 mnt_ns = get_task_mnt_ns_id(current);

  // Return directly if the current task is unconfined
  u32 *profile_mode = get_profile_mode(mnt_ns);
  if (profile_mode == NULL)
    return 0;

  // Prepare buffer
  struct buffer_offset offset = { .first_path = 0, .first_name = 0, .second_path = 0, .second_name = 0 };
  struct buffer *buf = get_buffer();
  if (buf == NULL)
    return 0;

  // Extract the dev path from the dev_name parameter
  prepend_string_to_first_block(dev_name, buf, &offset);

  // Extract the fstype from the type parameter
  prepend_fstype_to_third_block(type, buf);

  DEBUG_PRINT("================ lsm/sb_mount ================");
  DEBUG_PRINT("dev path: %s", buf->value);
  DEBUG_PRINT("offset: %d, length: %d", offset.first_path, offset.first_path-1);
  DEBUG_PRINT("dev name: %s, length: %d", &(buf->value[PATH_MAX*2]), offset.first_name);
  DEBUG_PRINT("fstype: %s", &(buf->value[PATH_MAX*3-FILE_SYSTEM_TYPE_MAX]));
  DEBUG_PRINT("flags: 0x%x", flags);

  if (*profile_mode == COMPLAIN_MODE) {
    // Record the behavior to the ringbuf
    struct audit_event *e;
    e = bpf_ringbuf_reserve(&v_audit_rb, sizeof(struct audit_event), 0);
    if (e) {
      DEBUG_PRINT("write audit event to ringbuf");
      e->action = ALLOWED_ACTION;
      e->type = MOUNT_TYPE;
      e->mnt_ns = mnt_ns;
      e->tgid = bpf_get_current_pid_tgid()>>32;
      e->ktime = bpf_ktime_get_boot_ns();
      bpf_probe_read_kernel_str(e->event_u.mount.path, offset.first_path & (PATH_MAX-1), buf->value);
      bpf_probe_read_kernel_str(e->event_u.mount.type, FILE_SYSTEM_TYPE_MAX, &(buf->value[PATH_MAX*3-FILE_SYSTEM_TYPE_MAX]));
      e->event_u.mount.flags = flags;
      bpf_ringbuf_submit(e, 0);
    }
    return 0;
  } else if (*profile_mode == ENFORCE_MODE) {
    // Return directly if there are no mount rules for the current task
    u32 *vmount_inner = get_mount_inner_map(mnt_ns);
    if (vmount_inner == NULL)
      return 0;

    if (flags & 
        (MS_REMOUNT | MS_BIND | MS_SHARED | MS_PRIVATE | MS_SLAVE | MS_UNBINDABLE | MS_MOVE | AA_MAY_UMOUNT)) {
      DEBUG_PRINT("force the fstype to 'none'");
      buf->value[PATH_MAX*3-FILE_SYSTEM_TYPE_MAX] = 'n';
      buf->value[PATH_MAX*3-FILE_SYSTEM_TYPE_MAX+1] = 'o';
      buf->value[PATH_MAX*3-FILE_SYSTEM_TYPE_MAX+2] = 'n';
      buf->value[PATH_MAX*3-FILE_SYSTEM_TYPE_MAX+3] = 'e';
      buf->value[PATH_MAX*3-FILE_SYSTEM_TYPE_MAX+4] = '\0';
    }

    // Iterate all rules in the inner map
    return iterate_mount_inner_map(vmount_inner, flags, buf, &offset, mnt_ns);
  } else {
    return 0;
  }
}

SEC("lsm/move_mount")
int BPF_PROG(varmor_move_mount, struct path *from_path, struct path *to_path) {
  // Retrieve the current task and its mnt ns id
  struct task_struct *current = (struct task_struct *)bpf_get_current_task();
  u32 mnt_ns = get_task_mnt_ns_id(current);

  // Return directly if the current task is unconfined
  u32 *profile_mode = get_profile_mode(mnt_ns);
  if (profile_mode == NULL)
    return 0;

  // Prepare buffer
  struct buffer_offset offset = { .first_path = 0, .first_name = 0, .second_path = 0, .second_name = 0 };
  struct buffer *buf = get_buffer();
  if (buf == NULL)
    return 0;
  
  // Extract the source path from the from_path parameter
  prepend_path_to_first_block(from_path->dentry, from_path->mnt, buf, &offset);

  // Mock flags and fstype
  // move_mount() is a part of the new system calls for mounting file systems 
  // since v5.2. See https://lwn.net/Articles/759499/ 
  // We only care about the relocation use case of move_mount() for now, and
  // reuse the rules for mount().
  buf->value[PATH_MAX*3-FILE_SYSTEM_TYPE_MAX] = 'n';
  buf->value[PATH_MAX*3-FILE_SYSTEM_TYPE_MAX+1] = 'o';
  buf->value[PATH_MAX*3-FILE_SYSTEM_TYPE_MAX+2] = 'n';
  buf->value[PATH_MAX*3-FILE_SYSTEM_TYPE_MAX+3] = 'e';
  buf->value[PATH_MAX*3-FILE_SYSTEM_TYPE_MAX+4] = '\0';

  DEBUG_PRINT("================ lsm/move_mount ================");
  DEBUG_PRINT("from path: %s, length: %d, from path offset: %d", 
      &(buf->value[offset.first_path & (PATH_MAX-1)]), PATH_MAX-offset.first_path-1, offset.first_path);
  DEBUG_PRINT("from name: %s, length: %d", &(buf->value[PATH_MAX*2]), offset.first_name);
  DEBUG_PRINT("mock fstype: %s", &(buf->value[PATH_MAX*3-FILE_SYSTEM_TYPE_MAX]));
  DEBUG_PRINT("mock flags: 0x%x", MS_MOVE);

  if (*profile_mode == COMPLAIN_MODE) {
    // Record the behavior to the ringbuf
    struct audit_event *e;
    e = bpf_ringbuf_reserve(&v_audit_rb, sizeof(struct audit_event), 0);
    if (e) {
      DEBUG_PRINT("write audit event to ringbuf");
      e->action = ALLOWED_ACTION;
      e->type = MOUNT_TYPE;
      e->mnt_ns = mnt_ns;
      e->tgid = bpf_get_current_pid_tgid()>>32;
      e->ktime = bpf_ktime_get_boot_ns();
      bpf_probe_read_kernel_str(e->event_u.mount.path, (PATH_MAX-offset.first_path) & (PATH_MAX-1), &(buf->value[offset.first_path & (PATH_MAX-1)]));
      bpf_probe_read_kernel_str(e->event_u.mount.type, FILE_SYSTEM_TYPE_MAX, &(buf->value[PATH_MAX*3-FILE_SYSTEM_TYPE_MAX]));
      e->event_u.mount.flags = MS_MOVE;
      bpf_ringbuf_submit(e, 0);
    }
    return 0;
  } else if (*profile_mode == ENFORCE_MODE) {
    // Return directly if there are no mount rules for the current task
    u32 *vmount_inner = get_mount_inner_map(mnt_ns);
    if (vmount_inner == NULL)
      return 0;

    // Iterate all rules in the inner map
    return iterate_mount_inner_map_extra(vmount_inner, MS_MOVE, buf, &offset, mnt_ns);
  } else {
    return 0;
  }
}

SEC("lsm/sb_umount")
int BPF_PROG(varmor_umount, struct vfsmount *mnt, int flags) {
  // Retrieve the current task and its mnt ns id
  struct task_struct *current = (struct task_struct *)bpf_get_current_task();
  u32 mnt_ns = get_task_mnt_ns_id(current);

  // Return directly if the current task is unconfined
  u32 *profile_mode = get_profile_mode(mnt_ns);
  if (profile_mode == NULL)
    return 0;

  // Prepare buffer
  struct buffer_offset offset = { .first_path = 0, .first_name = 0, .second_path = 0, .second_name = 0 };
  struct buffer *buf = get_buffer();
  if (buf == NULL)
    return 0;

  // Extract the source path from the from_path parameter
  struct mount *m = real_mount(mnt);
  struct dentry *dentry = BPF_CORE_READ(m, mnt).mnt_root;
  prepend_path_to_first_block(dentry, mnt, buf, &offset);

  // Mock flags and fstype
  // Linux mount-flags do not use the value 0x200, so we use it to identify umount
  buf->value[PATH_MAX*3-FILE_SYSTEM_TYPE_MAX] = 'n';
  buf->value[PATH_MAX*3-FILE_SYSTEM_TYPE_MAX+1] = 'o';
  buf->value[PATH_MAX*3-FILE_SYSTEM_TYPE_MAX+2] = 'n';
  buf->value[PATH_MAX*3-FILE_SYSTEM_TYPE_MAX+3] = 'e';
  buf->value[PATH_MAX*3-FILE_SYSTEM_TYPE_MAX+4] = '\0';

  DEBUG_PRINT("================ lsm/sb_umount ================");
  DEBUG_PRINT("umount path: %s, length: %d, umount path offset: %d", 
      &(buf->value[offset.first_path & (PATH_MAX-1)]), PATH_MAX-offset.first_path-1, offset.first_path);
  DEBUG_PRINT("umount name: %s, length: %d", &(buf->value[PATH_MAX*2]), offset.first_name);
  DEBUG_PRINT("mock fstype: %s", &(buf->value[PATH_MAX*3-FILE_SYSTEM_TYPE_MAX]));
  DEBUG_PRINT("mock flags: 0x%x", AA_MAY_UMOUNT);

  if (*profile_mode == COMPLAIN_MODE) {
    // Record the behavior to the ringbuf
    struct audit_event *e;
    e = bpf_ringbuf_reserve(&v_audit_rb, sizeof(struct audit_event), 0);
    if (e) {
      DEBUG_PRINT("write audit event to ringbuf");
      e->action = ALLOWED_ACTION;
      e->type = MOUNT_TYPE;
      e->mnt_ns = mnt_ns;
      e->tgid = bpf_get_current_pid_tgid()>>32;
      e->ktime = bpf_ktime_get_boot_ns();
      bpf_probe_read_kernel_str(e->event_u.mount.path, (PATH_MAX-offset.first_path) & (PATH_MAX-1), &(buf->value[offset.first_path & (PATH_MAX-1)]));
      bpf_probe_read_kernel_str(e->event_u.mount.type, FILE_SYSTEM_TYPE_MAX, &(buf->value[PATH_MAX*3-FILE_SYSTEM_TYPE_MAX]));
      e->event_u.mount.flags = AA_MAY_UMOUNT;
      bpf_ringbuf_submit(e, 0);
    }
    return 0;
  } else if (*profile_mode == ENFORCE_MODE) {
    // Return directly if there are no mount rules for the current task
    u32 *vmount_inner = get_mount_inner_map(mnt_ns);
    if (vmount_inner == NULL)
      return 0;

    // Iterate all rules in the inner map
    return iterate_mount_inner_map_extra(vmount_inner, AA_MAY_UMOUNT, buf, &offset, mnt_ns);
  } else {
    return 0;
  }
}
