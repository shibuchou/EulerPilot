#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char LICENSE[] SEC("license") = "GPL";

#define TASK_COMM_LEN	 16

struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __type(key, int);
    __type(value, int);
} perf SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 8192);
    __type(key, int);
    __type(value, struct pid_data);
} pid_map SEC(".maps");

struct pid_data {
    u64 prev_pid;    
    u64 time;  
    u64 next_pid;     
    char comm[TASK_COMM_LEN]; 
};

SEC("tracepoint/sched/sched_switch")
int trace_sched_switch(struct trace_event_raw_sched_switch *ctx)
{
    u64 myprev_pid = ctx->prev_pid;
    u64 mynext_pid = ctx->next_pid; 
    u64 ts = bpf_ktime_get_ns();

    struct task_struct *task = (struct task_struct *)bpf_get_current_task();
    if (!task)
        return 0;

    struct pid_data mydata = {};
    mydata.prev_pid = myprev_pid;
    mydata.next_pid = mynext_pid;
    mydata.time = ts;
    bpf_get_current_comm(&mydata.comm, sizeof(mydata.comm));
    bpf_perf_event_output(ctx, &perf, BPF_F_CURRENT_CPU, &mydata, sizeof(mydata));

    return 0;
}