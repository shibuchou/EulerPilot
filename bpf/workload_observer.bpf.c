// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
#include "vmlinux.h"

#include <bpf/bpf_core_read.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#include "workload_observer.h"

char LICENSE[] SEC("license") = "Dual BSD/GPL";

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, EULERPILOT_MAX_TASKS);
    __type(key, u32);
    __type(value, struct task_metrics);
} task_metrics_map SEC(".maps");

static __always_inline struct task_metrics *get_or_init_task(u32 pid, u32 tgid, const char *comm)
{
    struct task_metrics zero = {};
    struct task_metrics *metrics;

    zero.pid = pid;
    zero.tgid = tgid;
    bpf_probe_read_kernel_str(zero.comm, sizeof(zero.comm), comm);

    bpf_map_update_elem(&task_metrics_map, &pid, &zero, BPF_NOEXIST);
    metrics = bpf_map_lookup_elem(&task_metrics_map, &pid);
    if (!metrics)
        return NULL;

    metrics->pid = pid;
    metrics->tgid = tgid;
    bpf_probe_read_kernel_str(metrics->comm, sizeof(metrics->comm), comm);
    return metrics;
}

SEC("tp_btf/sched_wakeup")
int BPF_PROG(handle_sched_wakeup, struct task_struct *p)
{
    struct task_metrics *metrics;
    u32 pid = BPF_CORE_READ(p, pid);
    u32 tgid = BPF_CORE_READ(p, tgid);
    u64 now = bpf_ktime_get_ns();
    u64 cgroup_id = bpf_get_current_cgroup_id();
    char comm[EULERPILOT_COMM_LEN];

    BPF_CORE_READ_STR_INTO(&comm, p, comm);
    metrics = get_or_init_task(pid, tgid, comm);
    if (!metrics)
        return 0;

    metrics->wakeup_count += 1;
    metrics->last_enqueued_ns = now;
    metrics->cgroup_id = cgroup_id;
    return 0;
}

SEC("tp_btf/sched_switch")
int BPF_PROG(handle_sched_switch, bool preempt, struct task_struct *prev,
             struct task_struct *next, unsigned int prev_state)
{
    struct task_metrics *prev_metrics, *next_metrics;
    u64 now = bpf_ktime_get_ns();
    u32 prev_pid, prev_tgid, next_pid, next_tgid;
    char prev_comm[EULERPILOT_COMM_LEN];
    char next_comm[EULERPILOT_COMM_LEN];

    prev_pid = BPF_CORE_READ(prev, pid);
    prev_tgid = BPF_CORE_READ(prev, tgid);
    next_pid = BPF_CORE_READ(next, pid);
    next_tgid = BPF_CORE_READ(next, tgid);

    BPF_CORE_READ_STR_INTO(&prev_comm, prev, comm);
    BPF_CORE_READ_STR_INTO(&next_comm, next, comm);

    prev_metrics = get_or_init_task(prev_pid, prev_tgid, prev_comm);
    if (prev_metrics) {
        prev_metrics->ctx_switch_count += 1;
        prev_metrics->last_cpu = bpf_get_smp_processor_id();
    }

    next_metrics = get_or_init_task(next_pid, next_tgid, next_comm);
    if (next_metrics) {
        next_metrics->ctx_switch_count += 1;
        next_metrics->last_cpu = bpf_get_smp_processor_id();
        if (next_metrics->last_enqueued_ns && now > next_metrics->last_enqueued_ns)
            next_metrics->total_wait_ns += now - next_metrics->last_enqueued_ns;
        next_metrics->runtime_ns += 1000000;
    }

    return 0;
}

SEC("tp_btf/sched_migrate_task")
int BPF_PROG(handle_sched_migrate_task, struct task_struct *p, int new_cpu)
{
    struct task_metrics *metrics;
    u32 pid = BPF_CORE_READ(p, pid);
    u32 tgid = BPF_CORE_READ(p, tgid);
    char comm[EULERPILOT_COMM_LEN];

    BPF_CORE_READ_STR_INTO(&comm, p, comm);
    metrics = get_or_init_task(pid, tgid, comm);
    if (!metrics)
        return 0;

    metrics->migrate_count += 1;
    metrics->last_cpu = new_cpu;
    return 0;
}
