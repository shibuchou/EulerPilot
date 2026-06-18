#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#define TASK_COMM_LEN	 16
#define MAX_FILENAME_LEN 127

struct test {
	unsigned short common_type;
	unsigned char common_flags;
	unsigned char common_preempt_count;
	int common_pid;
	dev_t dev;
	sector_t sector;
	unsigned int nr_sector;
	int error;
	char rwbs[8];
};

struct mydata_t {
    int pid;
    int cpu;
    u64 insert_time;
    u64 issue_time;
    u64 complete_time;
    char comm[TASK_COMM_LEN];
    sector_t sector;
    unsigned long latency;
    unsigned long queue_time;
    unsigned long execution_time;
	char filename[MAX_FILENAME_LEN];
};

struct timedata {
    int pid;
    char comm[TASK_COMM_LEN];
    u64 insert_time;
    u64 issue_time;
    u64 complete_time;
};

char LICENSE[] SEC("license") = "Dual BSD/GPL";

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 8192);
	__type(key, sector_t);
	__type(value, struct timedata);
} time_bpf SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size, sizeof(pid_t));
    __uint(value_size, sizeof(int));
} mydata SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct mydata_t);
} heap SEC(".maps");


SEC("tp/block/block_rq_insert")
int handle_blk_insert(struct trace_event_raw_block_rq *insert_ctx){   
    struct mydata_t* d;
    struct timedata* t;
    int zero = 0;
    int cpu;
    cpu = bpf_get_smp_processor_id();
    d = bpf_map_lookup_elem(&heap, &zero);
	if (!d) /* can't happen */
		return 0;
    //请求发起信息获取
    pid_t pid;
    pid = bpf_get_current_pid_tgid() >> 32;
    u64 start_blk_insert;
    start_blk_insert = bpf_ktime_get_ns();//io请求发起时间
    //请求发起信息传入
    bpf_get_current_comm(&d->comm, sizeof(d->comm)); 
    d->pid = pid;
    d->cpu = cpu;
    d->insert_time = start_blk_insert;
    d->sector = insert_ctx->sector;
    //当前进程相关信息获取，作为识别信息标志
    t->pid = pid;
    t->insert_time = start_blk_insert;
    struct task_struct *task = (struct task_struct *)bpf_get_current_task();
    bpf_probe_read_str(&t->comm, sizeof(t->comm), task->comm);
    //信息更新与传递
    bpf_map_update_elem(&time_bpf, &d->sector, t, BPF_ANY);
    bpf_perf_event_output(insert_ctx, &mydata, BPF_F_CURRENT_CPU, d, sizeof(*d));
    return 0;
}

SEC("tp/block/block_rq_issue")
int handle_blk_issue(struct trace_event_raw_block_rq *issue_ctx){   
    struct mydata_t* d;
    sector_t sector;
    sector = issue_ctx->sector;
    int zero = 0;
    d = bpf_map_lookup_elem(&heap, &zero);
	if (!d) /* can't happen */
		return 0;
    bpf_map_delete_elem(&heap, &zero);
    //找出申请时间//找出标志
    struct timedata* t;
    t = bpf_map_lookup_elem(&time_bpf, &sector);
    bpf_map_delete_elem(&time_bpf, &sector);
    if (!t){
        bpf_printk("Opps!");
        return 0;
    }
    //开始执行io信息获取
    pid_t pid;
    pid = bpf_get_current_pid_tgid() >> 32;
    u64 start_blk_issue;
    start_blk_issue = bpf_ktime_get_ns();
    d->issue_time = start_blk_issue;//开始执行时间
    u64 queue_time;
    queue_time = d->issue_time - t->insert_time;
    //执行信息传递
    bpf_get_current_comm(&d->comm, sizeof(d->comm));
    d->pid = t->pid;
    d->insert_time = t->insert_time;
    d->queue_time = queue_time;
    d->sector = sector;
    d->issue_time = bpf_ktime_get_ns();
    //识别标志
    t->issue_time = start_blk_issue;
    struct task_struct *task = (struct task_struct *)bpf_get_current_task();
    bpf_probe_read_str(&t->comm, sizeof(t->comm), task->comm);
    //数据更新
    bpf_map_update_elem(&time_bpf, &d->sector, t, BPF_ANY);
    bpf_perf_event_output(issue_ctx, &mydata, BPF_F_CURRENT_CPU, d, sizeof(*d));
    return 0;
}

SEC("tp/block/block_rq_complete")
int handle_blk_complete(struct test *complete_ctx){
    struct mydata_t* d;
    pid_t pid;
    sector_t sector;
    sector = complete_ctx->sector;
    int zero = 0;
    d = bpf_map_lookup_elem(&heap, &zero);
	if (!d) /* can't happen */
		return 0;
    bpf_map_delete_elem(&heap, &zero);
    //找出申请时间//找出执行时间
    //找出标志
    struct timedata *t;
    t = bpf_map_lookup_elem(&time_bpf, &sector);
    bpf_map_delete_elem(&time_bpf, &sector);
    if (!t){
        bpf_printk("Opps!");
        return 0;
    }
    //完成io信息获取
    u64 finish_blk_complete;
    finish_blk_complete = bpf_ktime_get_ns();
    d->complete_time = finish_blk_complete;//完成io时间
    u64 execution_time;
    execution_time = d->complete_time - t->issue_time;//io执行时间
    u64 latency;
    latency = d->complete_time - t->insert_time;//io调度延迟
    //数据传递
    bpf_get_current_comm(&d->comm, sizeof(d->comm));
    d->pid = t->pid;
    d->insert_time = t->insert_time;
    d->issue_time = t->issue_time;
    d->latency = latency;
    d->execution_time = execution_time;
    d->sector = sector;
    //数据更新
    d->complete_time = bpf_ktime_get_ns();
    t->complete_time = finish_blk_complete;
    bpf_perf_event_output(complete_ctx, &mydata, BPF_F_CURRENT_CPU, d, sizeof(*d));
    return 0;
}