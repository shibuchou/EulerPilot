#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

char LICENSE[] SEC("license") = "Dual BSD/GPL";

struct {
	__uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
	__type(key, int);
	__type(value, int);
} perf SEC(".maps");

//对应的cpu和在这个cpu内这个进程的id
struct cpu_info {
	u64 cpu_id;
	u64 process_id;
};

struct process_info {
	int cpu_id;
	pid_t pid; // 进程的PID
	//u64 finish_t; //进程结束的时间
	u64 start_t; //进程开始的时间
	u64 used_t; //已经使用的CPU时间
	u64 total_t; //每个cpu占用的总时间
	u64 occ; //占用率
	u64 diff;
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 8192);
	__type(key, struct cpu_info);
	__type(value, struct process_info);
} pid_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 8192);
	__type(key, int);
	__type(value, u64);
} cpu_map SEC(".maps");

SEC("tracepoint/sched/sched_switch")
int trace_sched_switch(struct trace_event_raw_sched_switch *ctx)
{
	u64 prev_pid = ctx->prev_pid;
	u64 next_pid = ctx->next_pid; //获取当前进程的pid;

	u64 cpu_id = bpf_get_smp_processor_id(); // 获取当前 CPU ID

	// 构建键
	struct cpu_info prev_key = { .cpu_id = cpu_id, .process_id = prev_pid };
	struct cpu_info next_key = { .cpu_id = cpu_id, .process_id = next_pid };

	//在当前cpu上查找进程的信息
	struct process_info *prev_info = bpf_map_lookup_elem(&pid_map, &prev_key);
	struct process_info *next_info = bpf_map_lookup_elem(&pid_map, &next_key);

	u64 cur_time = bpf_ktime_get_ns(); //获取当前时间戳

	//如果前一个进程存在，cur_time为其结束时间，可以计算其本次占用时间
	if (prev_info) {
		// return 0;
		// 计算时间差，用当前时间（后一个进程的切入时间即前一个进程切出时间）减去前一个进程的切入时间
		u64 diff = cur_time - prev_info->start_t;
		prev_info->used_t = (prev_info->used_t) + diff; //已经使用过的时间叠加

		// 获取该CPU的总时间，并更新
		u64 *total_t = bpf_map_lookup_elem(&cpu_map, &cpu_id); //查找当前cpuID的总时间
		u64 tt;
		//查找键cpu_id，存在返回它的值，不存在创建一个新的键值对
		if (total_t == NULL) {
			// tt = 0;
			tt = 0;
		} else {
			tt = *total_t;
		}
		tt += diff;
		bpf_map_update_elem(&cpu_map, &cpu_id, &tt, BPF_ANY);
		prev_info->total_t = tt;
		prev_info->diff = diff;
		prev_info->occ = (prev_info->used_t * 100) / tt;
		bpf_perf_event_output(ctx, &perf, BPF_F_CURRENT_CPU, prev_info, sizeof(*prev_info));
	} // 如果是系统启动的第一个调度进程，则prev_info会为空，这里先不处理了

	// 处理后一个被调度的进程，cur_time为其开始时间
	if (!next_info) // 没有找到next_info，说明是第一次遇到该项，需要建一个初始化为0加进去

	{
		// return 0;
		struct process_info new_info = {};
		new_info.cpu_id = cpu_id;
		new_info.pid = next_pid;
		new_info.start_t = cur_time;
		new_info.occ = 0;
		// 更新新的信息
		bpf_map_update_elem(&pid_map, &next_key, &new_info, BPF_ANY);
	} else // 找到了更新其start时间
	{
		next_info->start_t = cur_time;
	}

	return 0;
}