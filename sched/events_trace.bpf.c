/* SPDX-License-Identifier: GPL-2.0 */
/*
 * events_trace - read-only tracing half of the R5 event link.
 *
 * Measures per-cgroup runnable wait the way delay accounting approximates it:
 * sched_wakeup records (timestamp, cgroup id, start_time) for the woken task,
 * sched_switch consumes it when that task first gets a CPU and adds the delta
 * to a per-cgroup aggregate. Nothing here writes scheduler state; the pump
 * (ep-evtmon) reads the aggregates once per window, resets them and reports.
 *
 * Known approximations (kept on purpose, they bound the cost):
 *  - a wakeup for an already-runnable task overwrites its timestamp, so the
 *    measured wait is "last wakeup to first run", not queue residence time;
 *  - tasks that exit before running leave a stale entry; it is removed on
 *    sched_process_exit and cannot be misattributed because start_time is
 *    checked before a delta is counted;
 *  - aggregates update a plain hash map, so very hot cgroups serialize a few
 *    instructions per wakeup on one entry. Measured cost belongs in evidence.
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

struct wait_key {
  __u64 ts_ns;      /* ktime at wakeup */
  __u64 cgroup_id;  /* kernfs id of the task's cgroup on the default hierarchy */
  __u64 start_time; /* task start_time: if it does not match, the pid was reused */
};

struct agg_value {
  __u64 events;
  __u64 wait_total_ns;
  __u64 wait_max_ns;
};

/* per-task wakeup record, keyed by pid (tid) */
struct {
  __uint(type, BPF_MAP_TYPE_HASH);
  __uint(max_entries, 65536);
  __type(key, __u32);
  __type(value, struct wait_key);
} wakeup_start SEC(".maps");

/* per-cgroup window aggregate, keyed by kernfs cgroup id; drained by the pump */
struct {
  __uint(type, BPF_MAP_TYPE_HASH);
  __uint(max_entries, 4096);
  __type(key, __u64);
  __type(value, struct agg_value);
} wait_agg SEC(".maps");

/* counters[0] = wakeups recorded, [1] = waits counted, [2] = pid-reuse discards,
 * [3] = aggregation updates lost (no space / contention) */
struct {
  __uint(type, BPF_MAP_TYPE_ARRAY);
  __uint(max_entries, 4);
  __type(key, __u32);
  __type(value, __u64);
} evt_stats SEC(".maps");

static __always_inline void count_stat(__u32 slot) {
  __u64 *v = bpf_map_lookup_elem(&evt_stats, &slot);
  if (v) __sync_fetch_and_add(v, 1);
}

static __always_inline int record_wakeup(struct task_struct *p) {
  if (p == NULL) return 0;
  __u32 pid = BPF_CORE_READ(p, pid);
  struct wait_key key = {};
  key.ts_ns = bpf_ktime_get_ns();
  struct cgroup *cgrp = BPF_CORE_READ(p, cgroups, dfl_cgrp);
  if (cgrp) key.cgroup_id = BPF_CORE_READ(cgrp, kn, id);
  key.start_time = BPF_CORE_READ(p, start_time);
  bpf_map_update_elem(&wakeup_start, &pid, &key, BPF_ANY);
  count_stat(0);
  return 0;
}

SEC("raw_tp/sched_wakeup")
int BPF_PROG(on_wakeup, struct task_struct *p) {
  return record_wakeup(p);
}

SEC("raw_tp/sched_wakeup_new")
int BPF_PROG(on_wakeup_new, struct task_struct *p) {
  return record_wakeup(p);
}

SEC("raw_tp/sched_switch")
int BPF_PROG(on_switch, bool preempt, struct task_struct *prev, struct task_struct *next) {
  if (next == NULL) return 0;
  __u32 pid = BPF_CORE_READ(next, pid);
  struct wait_key *key = bpf_map_lookup_elem(&wakeup_start, &pid);
  if (key == NULL) return 0;
  bpf_map_delete_elem(&wakeup_start, &pid);
  if (key->start_time != BPF_CORE_READ(next, start_time)) {
    count_stat(2); /* pid was reused between wakeup and dispatch; not our pair */
    return 0;
  }
  __u64 now = bpf_ktime_get_ns();
  if (now <= key->ts_ns) return 0;
  __u64 delta = now - key->ts_ns;

  struct agg_value zero = {};
  struct agg_value *agg = bpf_map_lookup_elem(&wait_agg, &key->cgroup_id);
  if (agg == NULL) {
    if (bpf_map_update_elem(&wait_agg, &key->cgroup_id, &zero, BPF_NOEXIST) != 0) {
      count_stat(3);
      return 0;
    }
    agg = bpf_map_lookup_elem(&wait_agg, &key->cgroup_id);
    if (agg == NULL) return 0;
  }
  __sync_fetch_and_add(&agg->events, 1);
  __sync_fetch_and_add(&agg->wait_total_ns, delta);
  __u64 old_max = agg->wait_max_ns;
  if (delta > old_max) agg->wait_max_ns = delta;
  count_stat(1);
  return 0;
}

SEC("raw_tp/sched_process_exit")
int BPF_PROG(on_exit, struct task_struct *p) {
  if (p == NULL) return 0;
  __u32 pid = BPF_CORE_READ(p, pid);
  bpf_map_delete_elem(&wakeup_start, &pid);
  return 0;
}
