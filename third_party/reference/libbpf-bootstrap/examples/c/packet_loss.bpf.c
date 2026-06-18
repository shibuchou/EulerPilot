#include "vmlinux.h"
#include "bpf/bpf_helpers.h"
#include "bpf/bpf_tracing.h"
#include "bpf/bpf_core_read.h"

char LICENSE[] SEC("license") = "Dual BSD/GPL";

struct skb_t {
    int cpu;
    u64 pid;
    u64 timestamp;
    size_t size;
    int gfp;
    int flags;
    u64 order;
    u64 migratetype;
    u64 pfn;
    u64 is_kmalloc; 
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1024);
	__type(key, int);
	__type(value,struct skb_t);
}skb_events SEC(".maps");

struct   {
    __uint(type ,BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size , sizeof(pid_t));
    __uint(value_size , sizeof(int));
}perf_map SEC(".maps");

SEC("tp/skb/kfree_skb")
int handle_sched_stat_runtime(struct trace_event_raw_kfree_skb *ctx){   
    struct skb_t* data;
    int cpu;
    cpu = bpf_get_smp_processor_id();
    data = bpf_map_lookup_elem(&skb_events, &cpu);
	if (!data) {
    return 0;
    }
    data->cpu = cpu;
    bpf_probe_read(&data->comm, sizeof(ctx->comm),&ctx->comm);
    data->pid = ctx->pid;
    data->runtime = ctx->runtime;
    data->vruntime = ctx->vruntime;
    bpf_map_update_elem(&skb_events, &data->cpu, data, BPF_ANY);
    bpf_perf_event_output(ctx, &perf_map, BPF_F_CURRENT_CPU, data, sizeof(*data));
    return 0;
}
