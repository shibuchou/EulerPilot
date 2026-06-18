#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>


#define TASK_COMM_LEN 16             //comm字段是一个长度为16的char型，故进程名最长为15个字符。

//psi计算时需要
#define PAGE_SHIFT  12
#define PAGE_SIZE 4096
#define PAGE_MASK (~(PAGE_SIZE-1))
#define PAGE_ALIGN(addr) (((addr) + PAGE_SIZE - 1) & PAGE_MASK)
#define NSEC_PER_USEC (1000L)
#define FSHIFT		11		/* nr of bits of precision */
#define FIXED_1		(1<<FSHIFT)	/* 1.0 as fixed-point */
#define LOAD_INT(x) ((x) >> FSHIFT)
#define LOAD_FRAC(x) LOAD_INT(((x) & (FIXED_1-1)) * 100)

int threshold = 0;
int trigger_resource;       /* 0: Io, 1: Mem, 2: Cpu, 3: any */
int trigger_window;         /* 0: avg10, 1: avg60, 2: avg300 */
int trigger_index;          /* 0: Some, 1: Full */
  //目前阈值为 PSI数据中 MEM 的avg10 大于10%时触发阈值
                      //对应于1秒中超过100ms时间因为进程争抢内存资源产生停顿

//psi_group数据结构
#define AVGNUM 18
#define TOTALNUM 12
	
enum subsystem {
	Io,
	Mem,
	Cpu,
};
enum index {
	Some,
	Full,
};


//全局变量，判断卡顿
int flag = 0;

char LICENSE[] SEC("license") = "Dual BSD/GPL";

static __always_inline int should_trigger(int res, int full, unsigned long avg0,
					  unsigned long avg1, unsigned long avg2)
{
	unsigned long avg = avg0;

	if (trigger_window == 1) {
		avg = avg1;
	} else if (trigger_window == 2) {
		avg = avg2;
	}

	if (full != trigger_index) {
		return 0;
	}
	if (trigger_resource != 3 && res != trigger_resource) {
		return 0;
	}
	return LOAD_INT(avg) >= threshold;
}

struct cpu_info {
	u64 cpu_id;			//!注释
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
};

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, 256 * 1024);      //!查大小
} rb SEC(".maps");

//对应的cpu和在这个cpu内这个进程的id

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 8192);              //!查大小
	__type(key, struct cpu_info);
	__type(value, struct process_info);
} pid_map SEC(".maps");						//!cpu和mem是否需要合并



struct pidmm_t {
    u32 tgid;
    u32 tid;
    u32 pid;
    int cpu;
    char prev_comm[16];			//!宏
    pid_t prev_pid;
    int prev_prio;
    long int prev_state;
    char next_comm[16];			//!宏
    pid_t next_pid;
    int next_prio;
    pgd_t pgd;
    long unsigned int total_vm;
    long unsigned int data;
    long unsigned int text;
    long unsigned int shared;
    long unsigned int resident;
};


struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, 8);
    __type(key, int);
    __type(value, struct pidmm_t);
} heap SEC(".maps");			//!改名



struct psidata{
	int pid;
	char comm[TASK_COMM_LEN];	// 进程名称
	int sub;					//IO or MEMORY 子系统
	int indexnum;					//IO or MEMORY 子系统
	unsigned long avg_p[3];		//avg10 avg60 avg300
	u64 total_p;				//总压力
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
	bool kflag;
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


SEC("kprobe/update_averages")
int BPF_KPROBE(update_averages,struct psi_group *group)
{	
	if (threshold==0){
		return 0;
	}

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

	unsigned long avg_t[AVGNUM];
	unsigned long total_t[TOTALNUM];

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
	// 定义一维数组的索引计算方式
	#define AVG_INDEX(res, full, w) ((res * 2 + full) * 3 + w)  //用一维数组解决二维数组
	#define TOTAL_INDEX(res, full) ((res * 2 + full) * 6)		

	flag = 0;

	// 处理数据
	int res, full, w;
	res = 0;
	if (res == Io) {
		psi_p->sub = Io;     
	} else if (res == Mem) {
		psi_p->sub = Mem;     
	} else if (res == Cpu) {
		psi_p->sub = Cpu;     
	}

	for (full = 0; full < ((res == Cpu) ? Mem : Cpu); full++) {
		unsigned long avg_p[3] = {0};
		u64 total_p = 0;

		if (full == 0) {
			psi_p->indexnum = Some;
		} else if (full == 1) {
			psi_p->indexnum = Full;
		}

		for (w = 0; w < 3; w++) {
			// 使用一维数组和计算得到的索引来访问元素
			avg_p[w] = avg_t[AVG_INDEX(res, full, w)];
			psi_p->avg_p[w] = avg_p[w];
		}
		// 计算total_p
		total_p = total_t[TOTAL_INDEX(res, full)] / NSEC_PER_USEC;
		psi_p->total_p = total_p;
			
		
		if(should_trigger(res, full, avg_p[0], avg_p[1], avg_p[2])){
			flag = 1;
		}	
		bpf_perf_event_output(ctx, &pb, BPF_F_CURRENT_CPU, psi_p, sizeof(*psi_p));
	}
		bpf_map_update_elem(&psi_flag, &zero, &flag, BPF_ANY);


	res = 1;
		if (res == Io) {
		psi_p->sub = Io;     
	} else if (res == Mem) {
		psi_p->sub = Mem;     
	} else if (res == Cpu) {
		psi_p->sub = Cpu;     
	}

	for (full = 0; full < ((res == Cpu) ? Mem : Cpu); full++) {
		unsigned long avg_p[3] = {0};
		u64 total_p = 0;

		if (full == 0) {
			psi_p->indexnum = Some;
		} else if (full == 1) {
			psi_p->indexnum = Full;
		}

		for (w = 0; w < 3; w++) {
			// 使用一维数组和计算得到的索引来访问元素
			avg_p[w] = avg_t[AVG_INDEX(res, full, w)];
			psi_p->avg_p[w] = avg_p[w];
		}
		// 计算total_p
		total_p = total_t[TOTAL_INDEX(res, full)] / NSEC_PER_USEC;
		psi_p->total_p = total_p;
			
		
		if(should_trigger(res, full, avg_p[0], avg_p[1], avg_p[2])){
			flag = 1;
		}	
		bpf_perf_event_output(ctx, &pb, BPF_F_CURRENT_CPU, psi_p, sizeof(*psi_p));
	}
		bpf_map_update_elem(&psi_flag, &zero, &flag, BPF_ANY);

	res=2;
		if (res == Io) {
		psi_p->sub = Io;     
	} else if (res == Mem) {
		psi_p->sub = Mem;     
	} else if (res == Cpu) {
		psi_p->sub = Cpu;     
	}

	for (full = 0; full < ((res == Cpu) ? Mem : Cpu); full++) {
		unsigned long avg_p[3] = {0};
		u64 total_p = 0;

		if (full == 0) {
			psi_p->indexnum = Some;
		} else if (full == 1) {
			psi_p->indexnum = Full;
		}

		for (w = 0; w < 3; w++) {
			// 使用一维数组和计算得到的索引来访问元素
			avg_p[w] = avg_t[AVG_INDEX(res, full, w)];
			psi_p->avg_p[w] = avg_p[w];
		}
		// 计算total_p
		total_p = total_t[TOTAL_INDEX(res, full)] / NSEC_PER_USEC;
		psi_p->total_p = total_p;
			
		
		if(should_trigger(res, full, avg_p[0], avg_p[1], avg_p[2])){
			flag = 1;
		}	
		bpf_perf_event_output(ctx, &pb, BPF_F_CURRENT_CPU, psi_p, sizeof(*psi_p));
	}
		bpf_map_update_elem(&psi_flag, &zero, &flag, BPF_ANY);


	return 0;
}



SEC("tracepoint/sched/sched_switch")
int trace_sched_switch(struct trace_event_raw_sched_switch *ctx)
{	
	if (threshold==0){
		return 0;
	}
    int key = 0;
    int* flag = bpf_map_lookup_elem(&psi_flag, &key);
	if (!flag ) { //错误处理
    	return 0; 
  	}
    u64 prev_pid ,next_pid;
    prev_pid = ctx->prev_pid;
	next_pid = ctx->next_pid;
    u64 id, *cur_time, diff, cpu_id;
	u64 pid, tid;
	id = bpf_get_current_pid_tgid();
	pid = id >> 32;//这里是线程ID即所谓tid
	tid = (u32)id;//这里实际上是线程组IDtgid
	diff = 0;
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
    struct pidmm_t* d = bpf_map_lookup_elem(&heap, &cpu_id);
    if (!d) {
        return 0;
    }
    
    struct cpu_info prev_key = { .cpu_id = cpu_id, .process_id = prev_pid };
	struct cpu_info next_key = { .cpu_id = cpu_id, .process_id = next_pid };
	struct process_info *prev_info, *next_info;
	prev_info = bpf_map_lookup_elem(&pid_map, &prev_key);
    if (prev_info){
        diff = bpf_ktime_get_ns() - prev_info->start_t;
        prev_info->used_t = diff;
		if(*flag==0){           //卡顿阈值判断
			return 0;
		}
        struct merge_events *prev_space;
		prev_space = bpf_ringbuf_reserve(&rb, sizeof(*prev_space), 0);
		if (!prev_space){
            return 0;
        }
	bpf_core_read(&mm, sizeof(task->active_mm),&task->active_mm);
    bpf_probe_read(&d->pid, sizeof(d->pid),&task->pid);
    bpf_probe_read(&d->tgid, sizeof(d->tgid),&task->tgid);
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
    bpf_map_update_elem(&heap, &cpu_id, d, BPF_ANY);
		/* 填补ringbuffer传递信息 */ 
		prev_space->kflag = true;
		prev_space->cpu_id = cpu_id;
		prev_space->pid = pid;
		prev_space->tid = tid;
		prev_space->used_t = prev_info->used_t;
		bpf_get_current_comm(&prev_space->comm, sizeof(prev_space->comm));
        prev_space->shared = d->shared;
		prev_space->data = d->data;
		prev_space->text = d->text;
		prev_space->resident = d->resident;
        prev_space->total_vm = d->total_vm;
		prev_space->task_tgid = d->tgid;
        prev_space->task_pid = d->pid;
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
    
    return 0;
    
}

