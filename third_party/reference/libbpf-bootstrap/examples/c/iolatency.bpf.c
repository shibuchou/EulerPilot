#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#define TASK_COMM_LEN	 16
#define MAX_FILENAME_LEN 127

struct iotest {
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

struct iodata {
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

struct meta{
    char comm[TASK_COMM_LEN];
    pid_t pid;
};

char LICENSE[] SEC("license") = "Dual BSD/GPL";

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 8192);
	__type(key, sector_t);
	__type(value, u64);
} blk_insert SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 8192);
	__type(key, sector_t);
	__type(value, u64);
} blk_issue SEC(".maps");


struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size, sizeof(pid_t));
    __uint(value_size, sizeof(int));
} mydata SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct iodata);
} io_heap SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1024);
	__type(key, sector_t);
	__type(value, struct meta);
} metaa SEC(".maps");

SEC("tp/block/block_rq_insert")
int handle_blk_insert(struct trace_event_raw_block_rq *insert_ctx){   
    struct iodata* d;
    struct meta m;
    int zero = 0;
    int cpu;
    cpu = bpf_get_smp_processor_id();
    d = bpf_map_lookup_elem(&io_heap, &zero);
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
    m.pid = pid;
    struct task_struct *task = (struct task_struct *)bpf_get_current_task();
    bpf_probe_read_str(&m.comm, sizeof(m.comm), task->comm);
    //信息更新与传递
    bpf_map_update_elem(&blk_insert, &d->sector, &start_blk_insert, BPF_ANY);
    bpf_map_update_elem(&metaa, &d->sector, &m, BPF_ANY);
    bpf_perf_event_output(insert_ctx, &mydata, BPF_F_CURRENT_CPU, d, sizeof(*d));
    return 0;
}

SEC("tp/block/block_rq_issue")
int handle_blk_issue(struct trace_event_raw_block_rq *issue_ctx){   
    struct iodata* d;
    sector_t sector;
    sector = issue_ctx->sector;
    int zero = 0;
    d = bpf_map_lookup_elem(&io_heap, &zero);
	if (!d) /* can't happen */
		return 0;
    bpf_map_delete_elem(&io_heap, &zero);
    //找出申请时间
    u64 *insert_time;
    insert_time = bpf_map_lookup_elem(&blk_insert, &sector);    
    if(insert_time==NULL){
        bpf_printk("cannot find insert_time");
        return 0;
    }
    //bpf_map_delete_elem(&blk_insert, &sector);
    //找出标志
    struct meta* m1;
    m1 = bpf_map_lookup_elem(&metaa, &sector);
    //bpf_map_delete_elem(&metaa, &sector);
    if (!m1){
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
    queue_time = d->issue_time - *insert_time;
    //执行信息传递
    bpf_get_current_comm(&d->comm, sizeof(d->comm));
    d->pid = m1->pid;
    d->insert_time = *insert_time;
    d->queue_time = queue_time;
    d->sector = sector;
    d->issue_time = bpf_ktime_get_ns();
    //识别标志
    //struct task_struct *task = (struct task_struct *)bpf_get_current_task();
    //bpf_probe_read_str(&m1->comm, sizeof(m1->comm), task->comm);
    //数据更新
    bpf_map_update_elem(&blk_issue, &d->sector, &start_blk_issue, BPF_ANY);
    bpf_map_update_elem(&metaa, &d->sector, m1, BPF_ANY);
    bpf_perf_event_output(issue_ctx, &mydata, BPF_F_CURRENT_CPU, d, sizeof(*d));
    return 0;
}

SEC("tp/block/block_rq_complete")
int handle_blk_complete(struct iotest *complete_ctx){
    struct iodata* d;
    pid_t pid;
    sector_t sector;
    sector = complete_ctx->sector;
    int zero = 0;
    d = bpf_map_lookup_elem(&io_heap, &zero);
	if (!d) /* can't happen */
		return 0;
    bpf_map_delete_elem(&io_heap, &zero);
    //找出申请时间
    u64 *insert_time;
    insert_time = bpf_map_lookup_elem(&blk_insert, &sector);    
    if(insert_time==NULL){
        bpf_printk("cannot find insert_time");
        return 0;
    }
    bpf_map_delete_elem(&blk_insert, &sector);
    //找出执行时间
    u64 *issue_time;
    issue_time = bpf_map_lookup_elem(&blk_issue, &sector);    
    if(issue_time==NULL){
        bpf_printk("cannot find issue_time");
        return 0;
    }
    bpf_map_delete_elem(&blk_issue, &sector);
    //找出标志
    struct meta *m2;
    m2 = bpf_map_lookup_elem(&metaa, &sector);
    bpf_map_delete_elem(&metaa, &sector);
    if (!m2){
        bpf_printk("Opps!");
        return 0;
    }
    //完成io信息获取
    u64 finish_blk_complete;
    finish_blk_complete = bpf_ktime_get_ns();
    d->complete_time = finish_blk_complete;//完成io时间
    u64 execution_time;
    execution_time = d->complete_time - *issue_time;//io执行时间
    u64 latency;
    latency = d->complete_time - *insert_time;//io调度延迟
    //数据传递
    bpf_get_current_comm(&d->comm, sizeof(d->comm));
    d->pid = m2->pid;
    d->insert_time = *insert_time;
    d->issue_time = *issue_time;
    d->latency = latency;
    d->execution_time = execution_time;
    d->sector = sector;
    //数据更新
    d->complete_time = bpf_ktime_get_ns();
    bpf_perf_event_output(complete_ctx, &mydata, BPF_F_CURRENT_CPU, d, sizeof(*d));
    return 0;
}