#include "vmlinux.h"

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

char LICENSE[] SEC("license") = "GPL";

#ifndef EPERM
#define EPERM 1
#endif

#define EVENT_LSM_FILE_OPEN 1
#define EVENT_EXECVE 2
#define EVENT_OPENAT 3
#define EVENT_CONNECT 4
#define EVENT_PTRACE 5
#define EVENT_LSM_BPRM_CHECK 6
#define MAX_SECURITY_PATH 256
#define MAX_SECURITY_TARGETS 8
#define SECURITY_TARGET_UNKNOWN 0xffffffff

struct security_policy_config {
    __u32 enforce;
    __u32 target_count;
};

struct security_policy_target {
    char file_path[MAX_SECURITY_PATH];
    char exec_path[MAX_SECURITY_PATH];
    __u64 cgroup_id;
};

struct security_policy_event {
    __u32 event_type;
    __u32 pid;
    __u32 tgid;
    __u32 enforce;
    __s32 decision;
    __u32 target_index;
    char comm[16];
    char path[256];
};

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct security_policy_config);
} policy_map SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, MAX_SECURITY_TARGETS);
    __type(key, __u32);
    __type(value, struct security_policy_target);
} target_map SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 1 << 20);
} events SEC(".maps");

static __always_inline void fill_common_event(struct security_policy_event *event,
                                              __u32 event_type,
                                              __u32 enforce,
                                              __s32 decision,
                                              __u32 target_index)
{
    __u64 pid_tgid = bpf_get_current_pid_tgid();

    event->event_type = event_type;
    event->pid = (__u32)pid_tgid;
    event->tgid = (__u32)(pid_tgid >> 32);
    event->enforce = enforce;
    event->decision = decision;
    event->target_index = target_index;
    bpf_get_current_comm(&event->comm, sizeof(event->comm));
}

static __always_inline int path_equals(const char *actual, const char *expected)
{
    for (int i = 0; i < MAX_SECURITY_PATH; i++) {
        if (expected[i] == '\0' && actual[i] == '\0')
            return 1;
        if (expected[i] == '\0' || actual[i] == '\0')
            return 0;
        if (actual[i] != expected[i])
            return 0;
    }
    return 0;
}

static __always_inline __u32 clamp_target_count(const struct security_policy_config *config)
{
    __u32 count = config ? config->target_count : 1;

    if (count == 0)
        count = 1;
    if (count > MAX_SECURITY_TARGETS)
        count = MAX_SECURITY_TARGETS;
    return count;
}

static __always_inline int target_scope_matches(const struct security_policy_target *target,
                                                __u64 current_cgroup_id)
{
    return target->cgroup_id == 0 || target->cgroup_id == current_cgroup_id;
}

static __always_inline int file_path_match_index(const char *path,
                                                 __u32 target_count,
                                                 __u64 current_cgroup_id)
{
    for (int i = 0; i < MAX_SECURITY_TARGETS; i++) {
        if ((__u32)i >= target_count)
            break;

        __u32 key = i;
        struct security_policy_target *target = bpf_map_lookup_elem(&target_map, &key);
        if (target && target->file_path[0] != '\0' &&
            target_scope_matches(target, current_cgroup_id) &&
            path_equals(path, target->file_path))
            return i;
    }
    return -1;
}

static __always_inline int exec_path_match_index(const char *path,
                                                 __u32 target_count,
                                                 __u64 current_cgroup_id)
{
    for (int i = 0; i < MAX_SECURITY_TARGETS; i++) {
        if ((__u32)i >= target_count)
            break;

        __u32 key = i;
        struct security_policy_target *target = bpf_map_lookup_elem(&target_map, &key);
        if (target && target->exec_path[0] != '\0' &&
            target_scope_matches(target, current_cgroup_id) &&
            path_equals(path, target->exec_path))
            return i;
    }
    return -1;
}

static __always_inline int is_self_agent(void)
{
    const char self_comm[] = "eulerpilot-agen";
    char comm[16];

    bpf_get_current_comm(&comm, sizeof(comm));
#pragma unroll
    for (int i = 0; i < sizeof(self_comm) - 1; i++) {
        if (comm[i] != self_comm[i])
            return 0;
    }
    return 1;
}

SEC("lsm/file_open")
int BPF_PROG(security_policy_demo, struct file *file, int ret)
{
    if (ret != 0)
        return ret;

    char path[256];
    long len = bpf_d_path(&file->f_path, path, sizeof(path));
    if (len <= 0)
        return 0;

    __u32 key = 0;
    struct security_policy_config *config = bpf_map_lookup_elem(&policy_map, &key);
    __u32 target_count = clamp_target_count(config);
    __u64 current_cgroup_id = bpf_get_current_cgroup_id();
    int target_index = file_path_match_index(path, target_count,
                                             current_cgroup_id);
    if (target_index < 0)
        return 0;

    __u32 enforce = config ? config->enforce : 1;
    __s32 decision = enforce ? -EPERM : 0;

    struct security_policy_event *event =
        bpf_ringbuf_reserve(&events, sizeof(*event), 0);
    if (event) {
        fill_common_event(event, EVENT_LSM_FILE_OPEN, enforce, decision,
                          (__u32)target_index);
        __builtin_memcpy(event->path, path, sizeof(event->path));
        bpf_ringbuf_submit(event, 0);
    }

    return decision;
}

SEC("lsm/bprm_check_security")
int BPF_PROG(security_policy_bprm, struct linux_binprm *bprm, int ret)
{
    if (ret != 0)
        return ret;

    const char *filename = BPF_CORE_READ(bprm, filename);
    if (!filename)
        return 0;

    char path[256];
    if (bpf_probe_read_kernel_str(path, sizeof(path), filename) <= 0)
        return 0;

    __u32 key = 0;
    struct security_policy_config *config = bpf_map_lookup_elem(&policy_map, &key);
    __u32 target_count = clamp_target_count(config);
    __u64 current_cgroup_id = bpf_get_current_cgroup_id();
    int target_index = exec_path_match_index(path, target_count,
                                             current_cgroup_id);
    if (target_index < 0)
        return 0;

    __u32 enforce = config ? config->enforce : 1;
    __s32 decision = enforce ? -EPERM : 0;

    struct security_policy_event *event =
        bpf_ringbuf_reserve(&events, sizeof(*event), 0);
    if (event) {
        fill_common_event(event, EVENT_LSM_BPRM_CHECK, enforce, decision,
                          (__u32)target_index);
        __builtin_memcpy(event->path, path, sizeof(event->path));
        bpf_ringbuf_submit(event, 0);
    }

    return decision;
}

SEC("tracepoint/syscalls/sys_enter_execve")
int trace_execve(struct trace_event_raw_sys_enter *ctx)
{
    const char *filename = (const char *)ctx->args[0];
    struct security_policy_event *event;

    if (!filename || is_self_agent())
        return 0;

    event = bpf_ringbuf_reserve(&events, sizeof(*event), 0);
    if (!event)
        return 0;

    fill_common_event(event, EVENT_EXECVE, 0, 0, SECURITY_TARGET_UNKNOWN);
    if (bpf_probe_read_user_str(event->path, sizeof(event->path), filename) <= 0)
        event->path[0] = '\0';
    bpf_ringbuf_submit(event, 0);
    return 0;
}

SEC("tracepoint/syscalls/sys_enter_openat")
int trace_openat(struct trace_event_raw_sys_enter *ctx)
{
    const char *filename = (const char *)ctx->args[1];
    struct security_policy_event *event;

    if (!filename || is_self_agent())
        return 0;

    event = bpf_ringbuf_reserve(&events, sizeof(*event), 0);
    if (!event)
        return 0;

    fill_common_event(event, EVENT_OPENAT, 0, 0, SECURITY_TARGET_UNKNOWN);
    if (bpf_probe_read_user_str(event->path, sizeof(event->path), filename) <= 0)
        event->path[0] = '\0';
    bpf_ringbuf_submit(event, 0);
    return 0;
}

SEC("tracepoint/syscalls/sys_enter_connect")
int trace_connect(struct trace_event_raw_sys_enter *ctx)
{
    struct security_policy_event *event;

    if (is_self_agent())
        return 0;

    event = bpf_ringbuf_reserve(&events, sizeof(*event), 0);
    if (!event)
        return 0;

    fill_common_event(event, EVENT_CONNECT, 0, 0, SECURITY_TARGET_UNKNOWN);
    __builtin_memcpy(event->path, "connect", sizeof("connect"));
    bpf_ringbuf_submit(event, 0);
    return 0;
}

SEC("tracepoint/syscalls/sys_enter_ptrace")
int trace_ptrace(struct trace_event_raw_sys_enter *ctx)
{
    struct security_policy_event *event;

    if (is_self_agent())
        return 0;

    event = bpf_ringbuf_reserve(&events, sizeof(*event), 0);
    if (!event)
        return 0;

    fill_common_event(event, EVENT_PTRACE, 0, 0, SECURITY_TARGET_UNKNOWN);
    __builtin_memcpy(event->path, "ptrace", sizeof("ptrace"));
    bpf_ringbuf_submit(event, 0);
    return 0;
}
