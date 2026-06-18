#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#define u64	  unsigned long long int
#ifndef TASK_COMM_LEN
	#define TASK_COMM_LEN 16
#endif
#define MAX_FILENAME_LEN 127

#define PAGE_SHIFT  12
#define PAGE_SIZE 4096
#define PAGE_MASK (~(PAGE_SIZE-1))
#define PAGE_ALIGN(addr) (((addr) + PAGE_SIZE - 1) & PAGE_MASK)

#define NSEC_PER_USEC (1000L)

#define FSHIFT		11		/* nr of bits of precision */
#define FIXED_1		(1<<FSHIFT)	/* 1.0 as fixed-point */
#define LOAD_INT(x) ((x) >> FSHIFT)
#define LOAD_FRAC(x) LOAD_INT(((x) & (FIXED_1-1)) * 100)

//total的值显示的不太合适

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
	// u64 total_t; //每个cpu占用的总时间
	u64 occ; //占用率
	char comm[TASK_COMM_LEN];
    u64 task_tgid;
    u64 task_pid;
    long unsigned int total_vm;
    long unsigned int data;
    long unsigned int text;
    long unsigned int shared;
    long unsigned int resident;
};

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, 256 * 1024);
} rb SEC(".maps");

//对应的cpu和在这个cpu内这个进程的id

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 8192);
	__type(key, struct cpu_info);
	__type(value, struct process_info);
} pid_map SEC(".maps");

// struct pidmm_t {
//     u32 tgid;
//     u32 tid;
//     u32 pid;
//     int cpu;
//     char prev_comm[16];
//     pid_t prev_pid;
//     int prev_prio;
//     long int prev_state;
//     char next_comm[16];
//     pid_t next_pid;
//     int next_prio;
//     pgd_t pgd;
//     long unsigned int total_vm;
//     long unsigned int data;
//     long unsigned int text;
//     long unsigned int shared;
//     long unsigned int resident;
// };

// struct {
//     __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
//     __uint(key_size, sizeof(pid_t));
//     __uint(value_size, sizeof(int));
// } mmevents SEC(".maps");

// struct {
//     __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
//     __uint(max_entries, 8);
//     __type(key, int);
//     __type(value, struct pidmm_t);
// } heap SEC(".maps");

// struct {
//     __uint(type, BPF_MAP_TYPE_HASH);
//     __uint(max_entries, 8192);
//     __type(key, int);
//     __type(value, struct pidmm_t);
// } mmdata SEC(".maps");

struct psidata{
	int pid;
	char comm[TASK_COMM_LEN];	// 进程名称
	int sub;					//IO or MEMORY 子系统
	int indexnum;					//IO or MEMORY 子系统
	char subsystem[10];			//IO or MEMORY 子系统
	char index[5];              //some or full 指标
	unsigned long avg_p[3];		//avg10 avg60 avg300
	u64 total_p;				//总压力
    int flag;
};

struct {
  __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
  __uint(key_size, sizeof(int));
  __uint(value_size, sizeof(int));
} pb SEC(".maps");

struct {
  __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
  __uint(max_entries, 1);
  __type(key, int);
  __type(value, struct psidata);
} psi_heap SEC(".maps");

struct {
  __uint(type, BPF_MAP_TYPE_HASH);
  __uint(max_entries, 8192);
  __type(key, int);
  __type(value, int);
} psi_flag SEC(".maps");


struct merge_events {
    u64 tid;
	u64 cpu_id;//
	u64 pid; // 进程的PID
	u64 start_t; //进程开始的时间
	u64 used_t; //已经使用的CPU时间
	u64 occ; //占用率
    char comm[TASK_COMM_LEN];
    u64 task_tgid;
    u64 task_pid;
	long unsigned int total_vm;
    long unsigned int data;
    long unsigned int text;
    long unsigned int shared;
    long unsigned int resident;
};

SEC("tracepoint/sched/sched_switch")
int trace_sched_switch(struct trace_event_raw_sched_switch *ctx)
{	
    // int zero = 0;
    // int *flag = bpf_map_lookup_elem(&psi_flag, &zero);
    // if(*flag == 1){
    u64 prev_pid ,next_pid;
    prev_pid = ctx->prev_pid;
	next_pid = ctx->next_pid;
    u64 id, cpu_id;
	u64 pid, tid;
	id = bpf_get_current_pid_tgid();
	pid = id >> 32;//这里是线程ID即所谓tid
	tid = (u32)id;//这里实际上是线程组IDtgid
	u64 diff = 0;
	cpu_id = bpf_get_smp_processor_id();
    long unsigned int stack_vm;
    long unsigned int start_code;
    long unsigned int end_code;
    long unsigned int filepage;
    long unsigned int shmempage;
    long unsigned int anonpage;
    struct mm_rss_stat rss_stat;
    struct task_struct *task = (struct task_struct *)bpf_get_current_task();
    struct mm_struct *mm;
    // struct pidmm_t* d = bpf_map_lookup_elem(&heap, &cpu_id);
    // if (!d) {
    //     return 0;
    // }
    struct cpu_info prev_key = { .cpu_id = cpu_id, .process_id = prev_pid };
	struct cpu_info next_key = { .cpu_id = cpu_id, .process_id = next_pid };
	struct process_info *prev_info, *next_info;
	prev_info = bpf_map_lookup_elem(&pid_map, &prev_key);
    if (prev_info){
        diff = bpf_ktime_get_ns() - prev_info->start_t;
        prev_info->used_t = diff;
        // struct process_info  *prev_space;
        struct merge_events *prev_space;
		prev_space = bpf_ringbuf_reserve(&rb, sizeof(*prev_space), 0);
		if (!prev_space){
            return 0;
        }
        bpf_core_read(&mm, sizeof(task->active_mm),&task->active_mm);
        bpf_probe_read(&prev_info->task_pid, sizeof(task->pid),&task->pid);
        bpf_probe_read(&prev_info->task_tgid, sizeof(task->tgid),&task->tgid);
        bpf_probe_read(&prev_info->total_vm, sizeof(mm->total_vm),&mm->total_vm);
        bpf_probe_read(&stack_vm, sizeof(mm->stack_vm),&mm->stack_vm);
        bpf_probe_read(&start_code, sizeof(mm->start_code),&mm->start_code);
        bpf_probe_read(&end_code, sizeof(mm->end_code),&mm->end_code);
        bpf_probe_read(&rss_stat, sizeof(mm->rss_stat),&mm->rss_stat);
        bpf_probe_read(&filepage, sizeof(rss_stat.count[0]),&rss_stat.count[0]);
        bpf_probe_read(&shmempage, sizeof(rss_stat.count[3]),&rss_stat.count[3]);
        bpf_probe_read(&anonpage, sizeof(rss_stat.count[1]),&rss_stat.count[1]);
        prev_info->shared = filepage + shmempage;
        prev_info->data = prev_info->total_vm - stack_vm;
        prev_info->text = (PAGE_ALIGN(end_code) - (start_code & PAGE_MASK)) >> PAGE_SHIFT;
        prev_info->resident = prev_info->shared + anonpage;
        bpf_map_update_elem(&pid_map, &prev_key, prev_info, BPF_ANY);
        // bpf_map_update_elem(&heap, &cpu_id, d, BPF_ANY);
		/* 填补ringbuffer传递信息 */ 
		prev_space->cpu_id = cpu_id;
		prev_space->pid = pid;
		prev_space->tid = tid;
		prev_space->used_t = prev_info->used_t;
		bpf_get_current_comm(&prev_space->comm, sizeof(prev_space->comm));
        prev_space->shared = prev_info->shared;
		prev_space->data = prev_info->data;
		prev_space->text = prev_info->text;
		prev_space->resident = prev_info->resident;
        prev_space->total_vm = prev_info->total_vm;
		prev_space->task_tgid = prev_info->task_tgid;
        prev_space->task_pid = prev_info->task_pid;
        /* ringbuffer传递信息*/
   		bpf_ringbuf_submit(prev_space, 0);
        //传递完之后删除map，每次只传一个进程，占用率在用户态处理
		bpf_map_delete_elem(&pid_map, &prev_key);
	}
    next_info = bpf_map_lookup_elem(&pid_map, &next_key);// 处理后一个被调度的进程，cur_time为其开始时间
	if (!next_info) // 没有找到next_info，说明是第一次遇到该项，需要建一个初始化为0加进去
	{
		struct process_info new_info = {};
		new_info.cpu_id = cpu_id;
		new_info.pid = pid;
		new_info.tid = tid;
		new_info.start_t = bpf_ktime_get_ns();
        new_info.used_t = 0;
		// 更新新的信息
		bpf_map_update_elem(&pid_map, &next_key, &new_info, BPF_ANY);
	} else {// 找到了更新其start时间
	   	next_info->start_t = bpf_ktime_get_ns();
		bpf_map_update_elem(&pid_map, &next_key, next_info, BPF_ANY);
    }
    // }else{
    //     struct merge_events *data_space;
	// 	data_space = bpf_ringbuf_reserve(&rb, sizeof(*data_space), 0);
	// 	if (!data_space){
    //         return 0;
    //     }
    //     data_space = NULL;
    //     bpf_ringbuf_submit(data_space, 0);
    //     return 0;
    // } 
    
    return 0;
    
}

SEC("kprobe/update_averages")
int BPF_KPROBE(update_averages,struct psi_group *group)
{
	struct psidata * psi_p;
	int zero = 0;
	psi_p = bpf_map_lookup_elem(&psi_heap, &zero);
	if (!psi_p) /* can't happen */
		return 0;

	pid_t pid;
	pid = bpf_get_current_pid_tgid() >> 32;
	psi_p->pid = pid;

	// 获取当前进程的名称（命令行）
 	if (bpf_get_current_comm(psi_p->comm, sizeof(psi_p->comm)))
    	psi_p->comm[0] = 0; // 如果获取失败，将 comm 字段置为空字符串

	// 将二维数组转换为一维数组
	unsigned long avg_t[18];
	unsigned long total_t[12];

	// 读取数据到一维数组中
	int err;
	err = bpf_core_read(&avg_t, sizeof(avg_t), &group->avg);
	if (err) {
		return 0;
	}
	err = bpf_core_read(&total_t, sizeof(total_t), &group->total);
	if (err) {
		return 0;
	}
        // struct merge_events *psi_space;
        // psi_space = bpf_ringbuf_reserve(&rb, sizeof(*psi_space), 0);
        // if (!psi_space)
        //     return 0;
	// 定义一维数组的索引计算方式
	#define AVG_INDEX(res, full, w) ((res * 2 + full) * 3 + w)
	#define TOTAL_INDEX(res, full) ((res * 2 + full) * 6)

	// 处理数据
	int res, full, w;
	for (res = 0; res < 3; res++) {
		if (res == 0) {
			psi_p->sub = 0;
		} else if (res == 1) {
			psi_p->sub = 1;
		} else if (res == 2) {
			psi_p->sub = 2;
		}

		for (full = 0; full < ((res == 2) ? 1 : 2); full++) {
			unsigned long avg_p[3] = {0};
			u64 total_p = 0;

			if (full == 0) {
				psi_p->indexnum = 0;
			} else if (full == 1) {
				psi_p->indexnum = 1;
			}

			for (w = 0; w < 3; w++) {
				// 使用一维数组和计算得到的索引来访问元素
				avg_p[w] = avg_t[AVG_INDEX(res, full, w)];
				psi_p->avg_p[w] = avg_p[w];
			}
						// 计算total_p
			total_p = total_t[TOTAL_INDEX(res, full)] / NSEC_PER_USEC;
			psi_p->total_p = total_p;
	                           
                // psi_space->psi_pid = psi_p->pid;
                // psi_space->sub = psi_p->sub;
                // psi_space->indexnum = psi_p->indexnum;
                // psi_space->total_p = psi_p->total_p;
                // bpf_probe_read(psi_space->psi_comm, sizeof(psi_p->comm), psi_p->comm);


				// bpf_printk(" total=%llu\n",
				// 	 LOAD_INT(avg_p[0]), LOAD_FRAC(avg_p[0]),
				// 	 LOAD_INT(avg_p[1]), LOAD_FRAC(avg_p[1]),
				// 	 LOAD_INT(avg_p[2]), LOAD_FRAC(avg_p[2]),
				// 	total_p);
                // bpf_ringbuf_submit(psi_space, 0);
                int flag = bpf_map_lookup_elem(&psi_flag, &zero);
                if(res ==1&& full== 0 && LOAD INT(avg p[0])>=10){
                    flag = 1;
                    psi_p->flag = flag;
                    bpf_map_update_elem(&psi_flag, &zero, &flag, BPF_ANY);
                }else{
                    flag = 0;
                    psi_p->flag = flag;
                    bpf_map_update_elem(&psi_flag, &zero, &flag, BPF_ANY);
                }
		
				bpf_perf_event_output(ctx, &pb, BPF_F_CURRENT_CPU, psi_p, sizeof(*psi_p));
			
		}
	}
	return 0;
}
