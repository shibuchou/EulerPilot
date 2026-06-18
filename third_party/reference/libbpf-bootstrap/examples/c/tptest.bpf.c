#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

struct runtime_t {
    /*int pid;*/
    int cpu;
    unsigned short common_type;      
    unsigned char common_flags;      
    unsigned char common_preempt_count;      
    int common_pid;  
    char comm[16];   
    pid_t pid;       
    u64 runtime;     
    u64 vruntime;    
    unsigned int cur;
};


char LICENSE[] SEC("license") = "Dual BSD/GPL";

struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size, sizeof(pid_t));
    __uint(value_size, sizeof(int));
} rtevents SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, 8);
	__type(key, int);
	__type(value, struct runtime_t);
} heap SEC(".maps");


SEC("tp/sched/sched_stat_runtime")
int handle_sched_stat_runtime(struct trace_event_raw_sched_stat_runtime *ctx,struct cpufreq_policy *policy){   
    struct runtime_t* d;
    int cpu;
    cpu = bpf_get_smp_processor_id();

    d = bpf_map_lookup_elem(&heap, &cpu);
	if (d) {
        d->cpu = cpu;
        // bpf_probe_read(&d->comm, sizeof(ctx->comm),&ctx->comm);
        d->pid = ctx->pid;
        d->runtime = ctx->runtime;
        d->vruntime = ctx->vruntime;
        
        /*pid_t pid;
        pid = bpf_get_current_pid_tgid() >> 32;*/
        //timestamp = bpf_ktime_get_ns();
        bpf_map_update_elem(&heap, &d->cpu, d, BPF_ANY);
        bpf_perf_event_output(ctx, &rtevents, BPF_F_CURRENT_CPU, d, sizeof(*d));
    }
    return 0;
}