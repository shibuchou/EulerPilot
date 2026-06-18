// lsm_kata.bpf.c
// 内核态 eBPF LSM 程序
// 功能：通过 bprm_check_security 控制 Kata 容器内进程 exec 行为

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

char LICENSE[] SEC("license") = "GPL";

#ifndef TASK_COMM_LEN
#define TASK_COMM_LEN 16
#endif

#define MAX_PATH_LEN 256
#define ACTION_ALLOW 0
#define ACTION_DENY  1

#define REASON_DISABLED 0
#define REASON_DENY_ALL 1
#define REASON_DENY_PATH 2
#define REASON_ALLOW 3

struct control_value {
    __u32 enabled;        // 0 = 策略关闭，1 = 策略开启
    __u32 deny_all_exec;  // 1 = 禁止所有 exec，测试时慎用
    __u32 audit_events;   // 1 = 输出事件日志
    __u32 reserved;
};

struct path_key {
    char path[MAX_PATH_LEN];
};

struct event {
    __u64 ts_ns;
    __u32 pid;
    __u32 tgid;
    __u32 action;
    __u32 reason;
    char comm[TASK_COMM_LEN];
    char filename[MAX_PATH_LEN];
};

// 控制 map：key 固定为 0
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct control_value);
} control_map SEC(".maps");

// 禁止执行路径 map
// key = 文件路径，例如 /usr/bin/date
// value = 1
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 128);
    __type(key, struct path_key);
    __type(value, __u32);
} deny_path_map SEC(".maps");

// 事件输出 ringbuf
struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 256 * 1024);
} events SEC(".maps");

static __always_inline void submit_event(const char *filename,
                                         __u32 action,
                                         __u32 reason)
{
    struct event *e;
    __u64 pid_tgid;

    e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
    if (!e)
        return;

    pid_tgid = bpf_get_current_pid_tgid();

    e->ts_ns = bpf_ktime_get_ns();
    e->pid = (__u32)pid_tgid;
    e->tgid = (__u32)(pid_tgid >> 32);
    e->action = action;
    e->reason = reason;

    bpf_get_current_comm(&e->comm, sizeof(e->comm));
    bpf_probe_read_kernel_str(e->filename, sizeof(e->filename), filename);

    bpf_ringbuf_submit(e, 0);
}

SEC("lsm/bprm_check_security")
int BPF_PROG(kata_bprm_check_security, struct linux_binprm *bprm, int ret)
{
    __u32 key = 0;
    __u32 *deny_flag;
    const char *filename;
    struct control_value *cfg;
    struct path_key pkey = {};

    // 如果前面的 LSM 已经拒绝了，这里保持拒绝结果
    if (ret != 0)
        return ret;

    cfg = bpf_map_lookup_elem(&control_map, &key);
    if (!cfg)
        return 0;

    filename = BPF_CORE_READ(bprm, filename);
    if (!filename)
        return 0;

    // 策略关闭：直接放行
    if (cfg->enabled == 0) {
        if (cfg->audit_events)
            submit_event(filename, ACTION_ALLOW, REASON_DISABLED);
        return 0;
    }

    // 策略开启，并且 deny_all_exec = 1：拒绝所有 exec
    if (cfg->deny_all_exec == 1) {
        if (cfg->audit_events)
            submit_event(filename, ACTION_DENY, REASON_DENY_ALL);
        return -13; // -EACCES
    }

    // 读取当前要执行的文件路径
    bpf_probe_read_kernel_str(pkey.path, sizeof(pkey.path), filename);

    // 查 deny_path_map，如果命中，拒绝执行
    deny_flag = bpf_map_lookup_elem(&deny_path_map, &pkey);
    if (deny_flag && *deny_flag == 1) {
        if (cfg->audit_events)
            submit_event(filename, ACTION_DENY, REASON_DENY_PATH);
        return -13; // -EACCES
    }

    // 没有命中策略，放行
    if (cfg->audit_events)
        submit_event(filename, ACTION_ALLOW, REASON_ALLOW);

    return 0;
}