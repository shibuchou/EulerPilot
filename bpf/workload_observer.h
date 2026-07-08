#pragma once

#ifdef __BPF__
#define EP_U32 __u32
#define EP_U64 __u64
#else
#include <stdint.h>
#define EP_U32 uint32_t
#define EP_U64 uint64_t
#endif

#define EULERPILOT_COMM_LEN 16
/* Bound the per-task metrics map to a VM-friendly size while still covering
 * benchmark and Kubernetes lab bursts without frequent key eviction.
 */
#define EULERPILOT_MAX_TASKS 16384

struct task_metrics {
    EP_U32 pid;
    EP_U32 tgid;
    EP_U64 cgroup_id;
    EP_U64 wakeup_count;
    EP_U64 total_wait_ns;
    EP_U64 runtime_ns;
    EP_U64 ctx_switch_count;
    EP_U64 migrate_count;
    EP_U64 last_enqueued_ns;
    EP_U32 last_cpu;
    char comm[EULERPILOT_COMM_LEN];
};
