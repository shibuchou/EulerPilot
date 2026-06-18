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

/*struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1024);
	__type(key, u64);
	__type(value, struct runtime_t);
} rts SEC(".maps");*/

SEC("tp/sched/sched_stat_runtime")
int handle_sched_stat_runtime(struct trace_event_raw_sched_stat_runtime *ctx,struct cpufreq_policy *policy){   
    struct runtime_t* d;
    int cpu;
    cpu = bpf_get_smp_processor_id();
    d = bpf_map_lookup_elem(&heap, &cpu);
	if (d) {
    d->cpu = cpu;
    bpf_probe_read(&d->comm, sizeof(ctx->comm),&ctx->comm);
    d->pid = ctx->pid;
    d->runtime = ctx->runtime;
    d->vruntime = ctx->vruntime;
    }else{
        return 0;
    }
    /*pid_t pid;
    pid = bpf_get_current_pid_tgid() >> 32;*/
    //timestamp = bpf_ktime_get_ns();
    bpf_map_update_elem(&heap, &d->cpu, d, BPF_ANY);
    bpf_perf_event_output(ctx, &rtevents, BPF_F_CURRENT_CPU, d, sizeof(*d));
    return 0;
}
SEC("kprobe/cpufreq_driver_target")
int BPF_KPROBE(cpufreq_driver_target, struct cpufreq_policy *policy/*,struct cpufreq_policy *new_policy*/){
    //struct cpufreq_policy* pol = policy;
    struct runtime_t* d;
    int cpu;
    cpu = bpf_get_smp_processor_id();
    unsigned int cpu1;
    bpf_core_read(&cpu1, sizeof(policy->cpu), &policy->cpu);
    d = bpf_map_lookup_elem(&heap, &cpu1);
	if (d) {
    d->cpu = cpu1;
    bpf_core_read_str(&d->cur, sizeof(policy->cur), &policy->cur);
    /*d->cur = pol->cur;*/
    }else{
        return 0;
    }
    bpf_map_update_elem(&heap, &d->cpu, d, BPF_ANY);
    bpf_perf_event_output(ctx, &rtevents, BPF_F_CURRENT_CPU, d, sizeof(*d));
    return 0;

    }