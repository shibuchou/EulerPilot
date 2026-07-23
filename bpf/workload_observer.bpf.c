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

static __always_inline u64 task_start_boottime_ns(struct task_struct *task)
{
    return BPF_CORE_READ(task, start_boottime);
}

static __always_inline struct task_metrics *get_or_init_task(struct task_struct *task)
{
    struct task_metrics zero = {};
    struct task_metrics *metrics;
    u32 pid = BPF_CORE_READ(task, pid);
    u32 tgid = BPF_CORE_READ(task, tgid);
    u64 start_boottime_ns = task_start_boottime_ns(task);

    zero.pid = pid;
    zero.tgid = tgid;
    zero.start_boottime_ns = start_boottime_ns;
    BPF_CORE_READ_STR_INTO(&zero.comm, task, comm);

    bpf_map_update_elem(&task_metrics_map, &pid, &zero, BPF_NOEXIST);
    metrics = bpf_map_lookup_elem(&task_metrics_map, &pid);
    if (!metrics)
        return NULL;

    if (metrics->start_boottime_ns != start_boottime_ns) {
        bpf_map_update_elem(&task_metrics_map, &pid, &zero, BPF_ANY);
        metrics = bpf_map_lookup_elem(&task_metrics_map, &pid);
        if (!metrics)
            return NULL;
    }

    metrics->pid = pid;
    metrics->tgid = tgid;
    metrics->start_boottime_ns = start_boottime_ns;
    BPF_CORE_READ_STR_INTO(&metrics->comm, task, comm);
    return metrics;
}

static __always_inline u64 task_default_cgroup_id(struct task_struct *task)
{
    /* The wakeup tracepoint runs in the waker context, so
     * bpf_get_current_cgroup_id() would attribute the event to the wrong task.
     * Read the target task's default cgroup id through task->cgroups instead.
     */
    return BPF_CORE_READ(task, cgroups, dfl_cgrp, kn, id);
}

SEC("tp_btf/sched_wakeup")
int BPF_PROG(handle_sched_wakeup, struct task_struct *p)
{
    struct task_metrics *metrics;
    u64 now = bpf_ktime_get_ns();
    u64 cgroup_id = task_default_cgroup_id(p);

    metrics = get_or_init_task(p);
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

    prev_metrics = get_or_init_task(prev);
    if (prev_metrics) {
        prev_metrics->ctx_switch_count += 1;
        prev_metrics->last_cpu = bpf_get_smp_processor_id();
        prev_metrics->cgroup_id = task_default_cgroup_id(prev);
        if (prev_metrics->last_running_ns && now > prev_metrics->last_running_ns)
            prev_metrics->runtime_ns += now - prev_metrics->last_running_ns;
        prev_metrics->last_running_ns = 0;
        /* sched_switch prev_state == 0 means TASK_RUNNING.  Preempted tasks
         * remain runnable and need a fresh queue-enter timestamp; otherwise
         * their next dispatch undercounts runqueue wait.
         */
        if (preempt || prev_state == 0)
            prev_metrics->last_enqueued_ns = now;
    }

    next_metrics = get_or_init_task(next);
    if (next_metrics) {
        next_metrics->ctx_switch_count += 1;
        next_metrics->last_cpu = bpf_get_smp_processor_id();
        next_metrics->cgroup_id = task_default_cgroup_id(next);
        if (next_metrics->last_enqueued_ns && now > next_metrics->last_enqueued_ns)
            next_metrics->total_wait_ns += now - next_metrics->last_enqueued_ns;
        next_metrics->last_enqueued_ns = 0;
        next_metrics->last_running_ns = now;
    }

    return 0;
}

SEC("tp_btf/sched_migrate_task")
int BPF_PROG(handle_sched_migrate_task, struct task_struct *p, int new_cpu)
{
    struct task_metrics *metrics;
    u32 pid = BPF_CORE_READ(p, pid);

    metrics = get_or_init_task(p);
    if (!metrics)
        return 0;

    metrics->migrate_count += 1;
    metrics->last_cpu = new_cpu;
    return 0;
}

SEC("tp_btf/sched_process_exit")
int BPF_PROG(handle_sched_process_exit, struct task_struct *p)
{
    u32 pid = BPF_CORE_READ(p, pid);

    bpf_map_delete_elem(&task_metrics_map, &pid);
    return 0;
}
