#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#define PAGE_SHIFT  12
#define PAGE_SIZE 4096
#define PAGE_MASK (~(PAGE_SIZE-1))
#define PAGE_ALIGN(addr) (((addr) + PAGE_SIZE - 1) & PAGE_MASK)

struct pidmm_t {
    u32 tgid;
    u32 tid;
    u32 pid;
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
    long unsigned int start_code;
    long unsigned int end_code;
    long unsigned int filepage;
    long unsigned int shmempage;
    long unsigned int anonpage;
    struct mm_rss_stat rss_stat;
    int cpu;
    cpu = bpf_get_smp_processor_id();
    d = bpf_map_lookup_elem(&heap, &cpu);
    if (!d) {
        return 0;
    }
    d->cpu = cpu; 
    d->prev_pid = bpf_get_current_pid_tgid()>>32;//高32位是PID     
    
    u32 tid = bpf_get_current_pid_tgid();//低32位是TID   当前线程的PID
    d->tid = tid;

    struct task_struct *task = (struct task_struct *)bpf_get_current_task();
    struct mm_struct *mm;
    bpf_core_read(&mm, sizeof(task->active_mm),&task->active_mm);

    bpf_probe_read(&d->pid, sizeof(d->pid),&task->pid);
    bpf_probe_read(&d->tgid, sizeof(d->tgid),&task->tgid);

    bpf_probe_read(&d->pgd, sizeof(d->pgd),&mm->pgd);
    bpf_probe_read(&d->total_vm, sizeof(mm->total_vm),&mm->total_vm);
    bpf_probe_read(&stack_vm, sizeof(mm->stack_vm),&mm->stack_vm);
    bpf_probe_read(&start_code, sizeof(mm->start_code),&mm->start_code);
    bpf_probe_read(&end_code, sizeof(mm->end_code),&mm->end_code);
    bpf_probe_read(&rss_stat, sizeof(mm->rss_stat),&mm->rss_stat);
    bpf_probe_read(&filepage, sizeof(rss_stat.count[0]),&rss_stat.count[0]);
    bpf_probe_read(&shmempage, sizeof(rss_stat.count[3]),&rss_stat.count[3]);
    bpf_probe_read(&anonpage, sizeof(rss_stat.count[1]),&rss_stat.count[1]);
    d->shared = filepage + shmempage;
    d->data = d->total_vm - stack_vm;
    d->text = (PAGE_ALIGN(end_code) - (start_code & PAGE_MASK)) >> PAGE_SHIFT;
    d->resident = d->shared + anonpage;
    //timestamp = bpf_ktime_get_ns();
    bpf_map_update_elem(&mmdata, &d->tgid, d, BPF_ANY);
    bpf_perf_event_output(ctx, &mmevents, BPF_F_CURRENT_CPU, d, sizeof(*d));
    return 0;
}


