#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#define PAGE_SHIFT	12
#define PAGE_SIZE 4096
#define PAGE_MASK (~(PAGE_SIZE-1))
#define PAGE_ALIGN(addr) (((addr) + PAGE_SIZE - 1) & PAGE_MASK)

struct pidmm_t {
    int pid;
    int cpu;
    char prev_comm[16];
	pid_t prev_pid;
	int prev_prio;
	long int prev_state;
	char next_comm[16];
	pid_t next_pid;
	int next_prio;
    pgd_t pgd;
    long unsigned int total_vm;
    long unsigned int data;
    long unsigned int text;
    long unsigned int shared;
    long unsigned int resident;
};


char LICENSE[] SEC("license") = "Dual BSD/GPL";

struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size, sizeof(pid_t));
    __uint(value_size, sizeof(int));
} mmevents SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, 8);
	__type(key, int);
	__type(value, struct pidmm_t);
} heap SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 8192);
	__type(key, int);
	__type(value, struct pidmm_t);
} mmdata SEC(".maps");

SEC("tp/sched/sched_switch")
int handle_sched_switch(struct trace_event_raw_sched_switch *ctx){   
    struct pidmm_t* d;
    long unsigned int stack_vm;
    long unsigned int data_vm;
    long unsigned int start_code;
	long unsigned int end_code;
    long unsigned int filepage;
	long unsigned int shmempage;
    long unsigned int anonpage;
    long unsigned int shared;
    long unsigned int total_vm;
    struct mm_rss_stat rss_stat;
    int cpu;
    cpu = bpf_get_smp_processor_id();
    d = bpf_map_lookup_elem(&heap, &cpu);
	if (!d) {
        return 0;
    }
    d->cpu = cpu; 
    d->prev_pid = ctx->prev_pid;
    pid_t pid;
    pid = bpf_get_current_pid_tgid() >> 32;
    d->pid = pid;
    struct task_struct *task = (struct task_struct *)bpf_get_current_task();
    struct mm_struct *mm;
    bpf_core_read(&mm, sizeof(task->active_mm),&task->active_mm);
    bpf_probe_read(&total_vm, sizeof(mm->total_vm),&mm->total_vm);
    bpf_probe_read(&stack_vm, sizeof(mm->stack_vm),&mm->stack_vm);
    bpf_probe_read(&data_vm, sizeof(mm->data_vm),&mm->data_vm);
    bpf_probe_read(&start_code, sizeof(mm->start_code),&mm->start_code);
    bpf_probe_read(&end_code, sizeof(mm->end_code),&mm->end_code);
    bpf_probe_read(&rss_stat, sizeof(mm->rss_stat),&mm->rss_stat);
    bpf_probe_read(&filepage, sizeof(rss_stat.count[0]),&rss_stat.count[0]);
    bpf_probe_read(&anonpage, sizeof(rss_stat.count[1]),&rss_stat.count[1]);
    bpf_probe_read(&shmempage, sizeof(rss_stat.count[3]),&rss_stat.count[3]);
    shared = filepage + shmempage;
    d->total_vm = total_vm;
    d->data = data_vm + stack_vm;
    d->text = (PAGE_ALIGN(end_code) - (start_code & PAGE_MASK)) >> PAGE_SHIFT;
    d->resident = shared + anonpage;
    d->shared = shared;
    bpf_map_update_elem(&mmdata, &d->pid, d, BPF_ANY);
    bpf_perf_event_output(ctx, &mmevents, BPF_F_CURRENT_CPU, d, sizeof(*d));
    return 0;
}