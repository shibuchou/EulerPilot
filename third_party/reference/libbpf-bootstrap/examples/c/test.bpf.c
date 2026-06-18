#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include "test.h"

char LICENSE[] SEC("license") = "Dual BSD/GPL";
struct cpu_info {
	u64 cpu_id;
	u64 process_id;
};

struct process_info {
	u64 tid;
	u64 cpu_id;
	u64 pid; // 进程的PID
	u64 start_t; //进程开始的时间
	u64 used_t; //已经使用的CPU时间
	u64 total_t; //每个cpu占用的总时间
	u64 occ; //占用率
	char comm[TASK_COMM_LEN];
	// long unsigned int resident;
	// long unsigned int shared;
};

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 8192);
    __type(key, pid_t);
    __type(value, u64);
} exec_start SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 256 * 1024);
} rb SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 8192);
	__type(key, struct cpu_info);
	__type(value, struct process_info);
} pid_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 8192);
	__type(key, struct cpu_info);
	__type(value, u64);
} cpu_map SEC(".maps");
const volatile unsigned long long min_duration_ns = 0;

SEC("tracepoint/sched/sched_switch")
int trace_sched_switch(struct trace_event_raw_sched_switch *ctx)
{
    u64 prev_pid ,next_pid;
    prev_pid = ctx->prev_pid;
	next_pid = ctx->next_pid;
    u64 id, *cur_time, diff, cpu_id, used;
	u64 pid, tid;
	id = bpf_get_current_pid_tgid();
	pid = id >> 32;
	tid = (u32)id;
	cpu_id = bpf_get_smp_processor_id();
    struct cpu_info prev_key = { .cpu_id = cpu_id, .process_id = prev_pid };
	struct cpu_info next_key = { .cpu_id = cpu_id, .process_id = next_pid };
	struct process_info *prev_info, *next_info, *prev_space;
    next_info = bpf_map_lookup_elem(&pid_map, &next_key);
	// 处理后一个被调度的进程，cur_time为其开始时间
	if (!next_info) // 没有找到next_info，说明是第一次遇到该项，需要建一个初始化为0加进去
	{
		struct process_info new_info = {};
		new_info.cpu_id = cpu_id;
		new_info.pid = next_pid;
		new_info.start_t = bpf_ktime_get_ns();
		new_info.occ = 0;
        new_info.used_t = 0;
        used = 0;
		// 更新新的信息
		bpf_map_update_elem(&pid_map, &next_key, &new_info, BPF_ANY);
	} else {// 找到了更新其start时间
	   prev_info = bpf_map_lookup_elem(&pid_map, &prev_key);
    if (prev_info){
        diff = bpf_ktime_get_ns() - next_info->start_t;
        used = (prev_info->used_t) + diff;
        prev_info->used_t = used;
    }
    }
	//如果前一个进程存在，cur_time为其结束时间，可以计算其本次占用时间
    prev_space = bpf_ringbuf_reserve(&rb, sizeof(*prev_space), 0);
    if (!prev_space)
        return 0;
    struct task_struct *task;
    /* fill out the sample with data */
    task = (struct task_struct *)bpf_get_current_task();
    prev_space->cpu_id = cpu_id;
    prev_space->pid = pid;
    prev_space->tid = tid;
    prev_space->used_t = used;
    bpf_get_current_comm(&prev_space->comm, sizeof(prev_space->comm));

    /* send data to user-space for post-processing */
    bpf_ringbuf_submit(prev_space, 0);
    
    return 0;
    
}

SEC("tp/sched/sched_process_exec")
int handle_exec(struct trace_event_raw_sched_process_exec *ctx)
{
    struct task_struct *task;
    unsigned fname_off;
    struct event *e;
    pid_t pid;
    u64 ts;

    /* remember time exec() was executed for this PID */
    pid = bpf_get_current_pid_tgid() >> 32;
    ts = bpf_ktime_get_ns();
    bpf_map_update_elem(&exec_start, &pid, &ts, BPF_ANY);

    /* don't emit exec events when minimum duration is specified */
    if (min_duration_ns)
        return 0;

    /* reserve sample from BPF ringbuf */
    e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
    if (!e)
        return 0;

    /* fill out the sample with data */
    task = (struct task_struct *)bpf_get_current_task();

    e->exit_event = false;
    e->pid = pid;
    e->ppid = BPF_CORE_READ(task, real_parent, tgid);
    bpf_get_current_comm(&e->comm, sizeof(e->comm));

    fname_off = ctx->__data_loc_filename & 0xFFFF;
    bpf_probe_read_str(&e->filename, sizeof(e->filename), (void *)ctx + fname_off);

    /* successfully submit it to user-space for post-processing */
    bpf_ringbuf_submit(e, 0);
    return 0;
}

SEC("tp/sched/sched_process_exit")
int handle_exit(struct trace_event_raw_sched_process_template* ctx)
{
    struct task_struct *task;
    struct event *e;
    pid_t pid, tid;
    u64 id, ts, *start_ts, duration_ns = 0;
    
    /* get PID and TID of exiting thread/process */
    id = bpf_get_current_pid_tgid();
    pid = id >> 32;
    tid = (u32)id;

    /* ignore thread exits */
    if (pid != tid)
        return 0;

    /* if we recorded start of the process, calculate lifetime duration */
    start_ts = bpf_map_lookup_elem(&exec_start, &pid);
    if (start_ts)
        duration_ns = bpf_ktime_get_ns() - *start_ts;
    else if (min_duration_ns)
        return 0;
    bpf_map_delete_elem(&exec_start, &pid);

    /* if process didn't live long enough, return early */
    if (min_duration_ns && duration_ns < min_duration_ns)
        return 0;

    /* reserve sample from BPF ringbuf */
    e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
    if (!e)
        return 0;

    /* fill out the sample with data */
    task = (struct task_struct *)bpf_get_current_task();

    e->exit_event = true;
    e->duration_ns = duration_ns;
    e->pid = pid;
    e->ppid = BPF_CORE_READ(task, real_parent, tgid);
    e->exit_code = (BPF_CORE_READ(task, exit_code) >> 8) & 0xff;
    bpf_get_current_comm(&e->comm, sizeof(e->comm));

    /* send data to user-space for post-processing */
    bpf_ringbuf_submit(e, 0);
    return 0;
}