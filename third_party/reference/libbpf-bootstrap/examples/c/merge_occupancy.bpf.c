#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#define MAX_ORDER 11

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


//阈值
#define Threshold 10  //目前阈值为 PSI数据中 MEM 的avg10 大于10%时触发阈值
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
//cpu_usage相关信息及map
struct cpu_info {
	u64 cpu_id;			//cpu号
	u64 process_id;		//基础呢很难过pid
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
	__uint(max_entries, 256 * 1024);      //!查大小
} rb SEC(".maps");

//对应的cpu和在这个cpu内这个进程的id

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 8192);              //!查大小
	__type(key, struct cpu_info);
	__type(value, struct process_info);
} pid_map SEC(".maps");			


//psi
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
//iolatency
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
	// char filename[MAX_FILENAME_LEN];
};

struct meta{
    char comm[TASK_COMM_LEN];
    pid_t pid;
};

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

//tpruntime
struct runtime_t {
    /*int pid;*/
    int cpu;      
    char comm[16];   
    pid_t pid;       
    u64 runtime;     
    u64 vruntime;    
    unsigned int cur;
};

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, 8);
	__type(key, int);
	__type(value, struct runtime_t);
} runtime_heap SEC(".maps");

//sched_dispatch
struct sched_switch_data {
    
    pid_t pid;
    pid_t prev_pid;
    //char prev_comm[16];
    int prev_prio;
    long prev_state;
    pid_t next_pid;
    //char next_comm[16];
    int next_prio;
    
};

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 8192);
    __type(key, int);
    __type(value, struct sched_switch_data);
} sched_switch_map SEC(".maps");

//kprobe_delay
struct cpu_delay_data {
    u64 pid;
    u64 enter_timestamp;    
    u64 time;  
    u64 run_timestamp;   
    char comm[TASK_COMM_LEN];  
};

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 8192);
    __type(key, int);
    __type(value, struct cpu_delay_data);
} cpu_delay_map SEC(".maps");

//buddy_stat
struct buddy_t {
    int pid;
    int cpu;
    u64 alloc_time;
    u64 free_time;
    unsigned int order;
    unsigned long num0;
    unsigned long num1;
    unsigned long num2;
    unsigned long num3;
    unsigned long num4;
    unsigned long num5;
    unsigned long num6;
    unsigned long num7;
    unsigned long num8;
    unsigned long num9;
    unsigned long num10;
    int migratetype;
	unsigned long count;
    unsigned int alloc_flags;
    struct zone *zone;
    struct free_area free_area[MAX_ORDER];
};

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct buddy_t);
} buddy_heap SEC(".maps");
//cpu_migration
struct migration_event {
    u64 pid1;
    u64 pid;
    u32 prev_cpu;
    u32 next_cpu;
    u32 current_cpu;
    struct cpumask cpumask;  // 增加 cpumask 结构
};

// struct {
//     __uint(type, BPF_MAP_TYPE_HASH);
//     __uint(max_entries, 1024);
//     __type(key, u32);
//     __type(value, struct migration_event);  
// } migration_events SEC(".maps");
//processlen
struct processlen_t {
    int pid;
    int cpu;
    unsigned int count;
};

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct processlen_t);
} rq_map SEC(".maps");
//合并
struct merge_events {
	bool kflag;
    //cpu_usage
    u64 tid;
	u64 cpu_id;
	u64 pid; // 进程的PID
	u64 start_t; //进程开始的时间
	u64 used_t; //已经使用的CPU时间
	u64 occ; //占用率
    char comm[TASK_COMM_LEN];
    //pidmm
    u64 task_tgid;
    u64 task_pid;
	long unsigned int total_vm;
    long unsigned int data;
    long unsigned int text;
    long unsigned int shared;
    long unsigned int resident;
    //iolatency
	int io_pid;
    int io_cpu;
    u64 insert_time;
    u64 issue_time;
    u64 complete_time;
    char io_comm[TASK_COMM_LEN];
    unsigned long io_latency;
    unsigned long queue_time;
    unsigned long execution_time;
    //tpruntime
    int runtime_cpu;      
    char runtime_comm[16];   
    pid_t runtime_pid;       
    u64 runtime;     
    u64 vruntime;    
    unsigned int cur;
    //sched_dispatch
    pid_t prev_pid;
    int prev_prio;
    long prev_state;
    pid_t next_pid;
    int next_prio;
    //kprobe_delay
    u64 cpu_latency;
    u64 cpu_delay_pid;
    char cpu_delay_comm[TASK_COMM_LEN]; 
    //buddy_stat
    unsigned long num0;
    unsigned long num1;
    unsigned long num2;
    unsigned long num3;
    unsigned long num4;
    unsigned long num5;
    unsigned long num6;
    unsigned long num7;
    unsigned long num8;
    unsigned long num9;
    unsigned long num10;
    //cpu_migration
    u64 migration_pid;
    u32 migration_prevcpu;
    u32 migration_nextcpu;
    u32 migration_currentcpu;
    struct cpumask cpumask;
    //processlen
    int processlen_cpu;
    unsigned int processlen_count;
};


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
	for (res = 0; res < 3; res++) {
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
                
            
            if(res == Mem && full == Some && LOAD_INT(avg_p[0]) >= Threshold ){
                flag = 1;
            }	
			bpf_perf_event_output(ctx, &pb, BPF_F_CURRENT_CPU, psi_p, sizeof(*psi_p));
		}
			bpf_map_update_elem(&psi_flag, &zero, &flag, BPF_ANY);
	}
	return 0;
}

//merge_latency
SEC("tracepoint/sched/sched_switch")
int trace_sched_switch(struct trace_event_raw_sched_switch *ctx)
{	
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
    struct cpu_info prev_key = { .cpu_id = cpu_id, .process_id = prev_pid };
	struct cpu_info next_key = { .cpu_id = cpu_id, .process_id = next_pid };
	struct process_info *prev_info, *next_info;
	prev_info = bpf_map_lookup_elem(&pid_map, &prev_key);
    if (prev_info){
        diff = bpf_ktime_get_ns() - prev_info->start_t;
        prev_info->used_t = diff;
        struct merge_events *prev_space;
		prev_space = bpf_ringbuf_reserve(&rb, sizeof(*prev_space), 0);
		if (!prev_space){
            return 0;
        }
		if(*flag==0){           //卡顿阈值判断
			prev_space->kflag = false;
			bpf_ringbuf_submit(prev_space,0);
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
		/* 填补ringbuffer传递信息 */ 
		prev_space->kflag = true;
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
    
    return 0;
    
}
//io_latency
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
	// bpf_ringbuf_reserve(&rb, sizeof(*d), 0);
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
	// bpf_ringbuf_submit(d, 0);
    //bpf_perf_event_output(insert_ctx, &mydata, BPF_F_CURRENT_CPU, d, sizeof(*d));
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
	// bpf_ringbuf_reserve(&rb, sizeof(*d), 0);
    d->issue_time = start_blk_issue;//开始执行时间
    u64 queue_time;
    queue_time = d->issue_time - *insert_time;
    //执行信息传递
    
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
	// bpf_ringbuf_submit(d, 0);
    // bpf_perf_event_output(issue_ctx, &mydata, BPF_F_CURRENT_CPU, d, sizeof(*d));
    return 0;
}

SEC("tp/block/block_rq_complete")
int handle_blk_complete(struct iotest *complete_ctx){
    int key = 0;
    int* flag = bpf_map_lookup_elem(&psi_flag, &key);
	if (!flag ) { //错误处理
    	return 0; 
  	}
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
	u64 queue_time;
    queue_time = d->issue_time - *insert_time;//io申请延迟
    //数据传递
    bpf_get_current_comm(&d->comm, sizeof(d->comm));
	struct merge_events *io_space;
    if (!io_space){
            return 0;
        }
	bpf_ringbuf_reserve(&rb, sizeof(*io_space), 0);
    if(*flag==0){           //卡顿阈值判断
			io_space->kflag = false;
			bpf_ringbuf_submit(io_space,0);
			return 0;
		}
    d->pid = m2->pid;
    d->insert_time = *insert_time;
    d->issue_time = *issue_time;
    d->latency = latency;
    d->execution_time = execution_time;
    d->sector = sector;
    //数据更新
    d->complete_time = bpf_ktime_get_ns();
	io_space->io_pid = d->pid;
	io_space->io_cpu = d->cpu;
	io_space->insert_time = d->insert_time;
	io_space->issue_time = d->issue_time;
	io_space->complete_time = d->complete_time;
	io_space->io_latency = d->latency;
	io_space->execution_time = d->execution_time;
	io_space->queue_time = d->queue_time;
	bpf_get_current_comm(&io_space->io_comm, sizeof(io_space->io_comm));
	bpf_ringbuf_submit(io_space, 0);
    // bpf_perf_event_output(complete_ctx, &mydata, BPF_F_CURRENT_CPU, d, sizeof(*d));
    return 0;
}
//tpruntime
SEC("tp/sched/sched_stat_runtime")
int handle_sched_stat_runtime(struct trace_event_raw_sched_stat_runtime *ctx,struct cpufreq_policy *policy){   
    int key = 0;
    int* flag = bpf_map_lookup_elem(&psi_flag, &key);
	if (!flag ) { //错误处理
    	return 0; 
  	}
    struct runtime_t* d;
    int cpu;
    cpu = bpf_get_smp_processor_id();
    d = bpf_map_lookup_elem(&runtime_heap, &cpu);
	if (d) {
    d->cpu = cpu;
    bpf_probe_read(&d->comm, sizeof(ctx->comm),&ctx->comm);
    d->pid = ctx->pid;
    d->runtime = ctx->runtime;
    d->vruntime = ctx->vruntime;
    }else{
        return 0;
    }
    bpf_map_update_elem(&runtime_heap, &d->cpu, d, BPF_ANY);
    struct merge_events *space;
    if (!space){
            return 0;
        }
	bpf_ringbuf_reserve(&rb, sizeof(*space), 0);
    if(*flag==0){           //卡顿阈值判断
			space->kflag = false;
			bpf_ringbuf_submit(space,0);
			return 0;
		}
    space->runtime_cpu = d->cpu;
    space->runtime_pid = d->pid;
    space->runtime = d->runtime;
    space->vruntime = d->vruntime;
    space->cur = d->cur;
	bpf_ringbuf_submit(space, 0);
    // bpf_perf_event_output(ctx, &rtevents, BPF_F_CURRENT_CPU, d, sizeof(*d));
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
    d = bpf_map_lookup_elem(&runtime_heap, &cpu1);
	if (d) {
    d->cpu = cpu1;
    bpf_core_read_str(&d->cur, sizeof(policy->cur), &policy->cur);
    /*d->cur = pol->cur;*/
    }else{
        return 0;
    }
    bpf_map_update_elem(&runtime_heap, &d->cpu, d, BPF_ANY);

    // bpf_perf_event_output(ctx, &rtevents, BPF_F_CURRENT_CPU, d, sizeof(*d));
    return 0;

    }
//sched_dispatch
SEC("tp/sched/sched_switch")
int myprog(struct trace_event_raw_sched_switch *ctx)
{
    int key = 0;
    int* flag = bpf_map_lookup_elem(&psi_flag, &key);
	if (!flag ) { //错误处理
    	return 0; 
  	}
    struct sched_switch_data *data ;
    pid_t pid=bpf_get_current_pid_tgid() >> 32;
    
    data = bpf_map_lookup_elem(&sched_switch_map, &pid);
	if (!data) /* can't happen */
		return 0;
    struct task_struct *tmp = (struct task_struct *)bpf_get_current_task();
    if(!tmp){
        return 0;
    }
    data->pid = bpf_get_current_pid_tgid() >> 32;
    data->prev_pid = ctx->prev_pid;
    //data->prev_comm[] = ctx->prev_comm;
    data->prev_prio = ctx->prev_prio;
    data->prev_state=ctx->prev_state;

    data->next_pid = ctx->next_pid; 
    //data->next_comm = ctx->next_comm;
    data->next_prio = ctx->next_prio;

    struct merge_events *space;
    if (!space){
            return 0;
        }
	bpf_ringbuf_reserve(&rb, sizeof(*space), 0);
    if(*flag==0){           //卡顿阈值判断
			space->kflag = false;
			bpf_ringbuf_submit(space,0);
			return 0;
		}
    space->prev_pid = data->prev_pid;
    space->prev_prio = data->prev_prio;
    space->prev_state = data->prev_state;
    space->next_pid = data->next_pid;
    space->next_prio = data->next_prio;
	bpf_ringbuf_submit(space, 0);
    // bpf_perf_event_output(ctx, &perf, BPF_F_CURRENT_CPU, data, sizeof(*data));
    return 0;
}
//kprobe_delay
SEC("kprobe/enqueue_task_fair")
int kprobe_enqueue_task_fair(struct pt_regs *ctx) 
{
    struct cpu_delay_data my_data = {};
    u64 timestamp = bpf_ktime_get_ns();
    u64 pid = bpf_get_current_pid_tgid() >> 32;
    my_data.enter_timestamp = bpf_ktime_get_ns();
    bpf_map_update_elem(&cpu_delay_map, &pid, &my_data, BPF_ANY);
    return 0;
}

SEC("kprobe/dequeue_task_fair")
int kprobe_dequeue_task_fair(struct pt_regs *ctx) 
{
    int key = 0;
    int* flag = bpf_map_lookup_elem(&psi_flag, &key);
	if (!flag ) { //错误处理
    	return 0; 
  	}
    u64 pid = bpf_get_current_pid_tgid() >> 32;
    u64 ts = bpf_ktime_get_ns();
    struct cpu_delay_data *mydata = bpf_map_lookup_elem(&cpu_delay_map, &pid);
    if(mydata){
        if(mydata->enter_timestamp == 0)  return 0;
        mydata->pid = pid;
        bpf_get_current_comm(&mydata->comm, sizeof(mydata->comm));
        mydata->run_timestamp = ts;
        mydata->time = mydata->run_timestamp - mydata->enter_timestamp;
        struct merge_events *space;
    if (!space){
            return 0;
        }
	    bpf_ringbuf_reserve(&rb, sizeof(*space), 0);
        if(*flag==0){           //卡顿阈值判断
			space->kflag = false;
			bpf_ringbuf_submit(space,0);
			return 0;
		}
        space->cpu_latency= mydata->time;
        space->cpu_delay_pid = mydata->pid;
        bpf_probe_read_str(&space->cpu_delay_comm, sizeof(space->cpu_delay_comm), &mydata->comm);
        bpf_ringbuf_submit(space, 0);
        // bpf_perf_event_output(ctx, &perf, BPF_F_CURRENT_CPU, mydata, sizeof(*mydata));
        bpf_map_delete_elem(&cpu_delay_map, &pid);  
    }
    else{
        return 0;
    }
    return 0;
}
//buddy_stat
SEC("kprobe/rmqueue_bulk")
int BPF_KPROBE(rmqueue_bulk, struct zone *zone, unsigned int order,
			unsigned long count, struct list_head *list,
			int migratetype, unsigned int alloc_flags){
    int key = 0;
    int* flag = bpf_map_lookup_elem(&psi_flag, &key);
	if (!flag ) { //错误处理
    	return 0; 
  	}
    struct buddy_t *d;
    int zero = 0;
    d = bpf_map_lookup_elem(&buddy_heap, &zero);
	if (!d) /* can't happen */
		return 0;
    int cpu;
    cpu = bpf_get_smp_processor_id();
    d->cpu = cpu;
    pid_t pid;
    pid = bpf_get_current_pid_tgid() >> 32;
    u64 alloc_time;
    alloc_time = bpf_ktime_get_ns();
    d->pid = pid;
    d->alloc_time = alloc_time;
    d->zone = zone;
    bpf_probe_read_str(&d->free_area[0], sizeof(zone->free_area[0]), &zone->free_area[0]);
    d->num0 = d->free_area[0].nr_free;
    bpf_probe_read_str(&d->free_area[1], sizeof(zone->free_area[1]), &zone->free_area[1]);
    d->num1 = d->free_area[1].nr_free;
    bpf_probe_read_str(&d->free_area[2], sizeof(zone->free_area[2]), &zone->free_area[2]);
    d->num2 = d->free_area[2].nr_free;
    bpf_probe_read_str(&d->free_area[3], sizeof(zone->free_area[3]), &zone->free_area[3]);
    d->num3 = d->free_area[3].nr_free;
    bpf_probe_read_str(&d->free_area[4], sizeof(zone->free_area[4]), &zone->free_area[4]);
    d->num4 = d->free_area[4].nr_free;
    bpf_probe_read_str(&d->free_area[5], sizeof(zone->free_area[5]), &zone->free_area[5]);
    d->num5 = d->free_area[5].nr_free;
    bpf_probe_read_str(&d->free_area[6], sizeof(zone->free_area[6]), &zone->free_area[6]);
    d->num6 = d->free_area[6].nr_free;
    bpf_probe_read_str(&d->free_area[7], sizeof(zone->free_area[7]), &zone->free_area[7]);
    d->num7 = d->free_area[7].nr_free;
    bpf_probe_read_str(&d->free_area[8], sizeof(zone->free_area[8]), &zone->free_area[8]);
    d->num8 = d->free_area[8].nr_free;
    bpf_probe_read_str(&d->free_area[9], sizeof(zone->free_area[9]), &zone->free_area[9]);
    d->num9 = d->free_area[9].nr_free;
    bpf_probe_read_str(&d->free_area[10], sizeof(zone->free_area[10]), &zone->free_area[10]);
    d->num10 = d->free_area[10].nr_free;
    d->count = count;
    d->order = order;
    d->migratetype = migratetype;
    bpf_map_update_elem(&buddy_heap, &migratetype, d, BPF_ANY);
    struct merge_events *space;
    if (!space){
            return 0;
        }
	    bpf_ringbuf_reserve(&rb, sizeof(*space), 0);
    if(*flag==0){           //卡顿阈值判断
			space->kflag = false;
			bpf_ringbuf_submit(space,0);
			return 0;
		}
    space->num0 = d->num0;
    space->num1 = d->num1;
    space->num2 = d->num2;
    space->num3 = d->num3;
    space->num4 = d->num4;
    space->num5 = d->num5;
    space->num6 = d->num6;    
    space->num7 = d->num7;
    space->num8 = d->num8;
    space->num9 = d->num9;
    space->num10 = d->num10;
    bpf_ringbuf_submit(space, 0);
    // bpf_perf_event_output(ctx, &myevents, BPF_F_CURRENT_CPU, d, sizeof(*d));
    return 0;
       
}
//cpu_migration
SEC("tp/sched/sched_migrate_task")
void handle_migrate(struct trace_event_raw_sched_migrate_task *ctx)
{
    int key = 0;
    int* flag = bpf_map_lookup_elem(&psi_flag, &key);
	if (!flag ) { //错误处理
    	return; 
  	}
    struct task_struct *task = (struct task_struct *)bpf_get_current_task();
    //u64 pid1 = bpf_get_current_pid_tgid() >> 32;
    u64 pid = ctx->pid;
    u32 prev_cpu = ctx->orig_cpu;
    u32 next_cpu = ctx->dest_cpu;
    u32 current_cpu = bpf_get_smp_processor_id();

    struct migration_event *event ;
    //event.pid1 = pid1;
    event->pid = pid;
    event->prev_cpu = prev_cpu;
    event->next_cpu = next_cpu;
    event->current_cpu = current_cpu;
    bpf_probe_read_kernel(&event->cpumask, sizeof(struct cpumask), &task->cpus_mask);  // 读取 cpumask 数据
    struct merge_events *space;
    if (!space){
            return;
        }
	bpf_ringbuf_reserve(&rb, sizeof(*space), 0);
    if(*flag==0){           //卡顿阈值判断
		space->kflag = false;
		bpf_ringbuf_submit(space,0);
		return;
	    }
    space->migration_pid = event->pid;
    space->migration_prevcpu = event->prev_cpu;
    space->migration_nextcpu = event->next_cpu;
    space->migration_currentcpu = event->current_cpu;
    space->cpumask = event->cpumask;
    bpf_ringbuf_submit(space, 0);
    // bpf_perf_event_output(ctx, &migrations, BPF_F_CURRENT_CPU, &event, sizeof(event));
}
//processlen
SEC("kprobe/update_rq_clock")
int BPF_KPROBE(update_rq_clock,struct rq *rq){
    struct processlen_t *mydata;
    int key = 0;
    int* flag = bpf_map_lookup_elem(&psi_flag, &key);
	if (!flag ) { //错误处理
    	return 0; 
  	}
    int rqKey = 0;
    int cpu;
    cpu = bpf_get_smp_processor_id();
    mydata = bpf_map_lookup_elem(&rq_map, &rqKey);
    if (!mydata) {
    return 0;
    }
else{
    bpf_probe_read_str(&mydata->count,sizeof(rq->nr_running),&rq->nr_running);
    mydata->cpu = cpu;
    struct merge_events *space;
    if (!space){
            return 0;
        }
	bpf_ringbuf_reserve(&rb, sizeof(*space), 0);
    if(*flag==0){           //卡顿阈值判断
		space->kflag = false;
		bpf_ringbuf_submit(space,0);
		return 0;
	    }
    space->processlen_cpu = mydata->cpu;
    space->processlen_count = mydata->count;
    bpf_ringbuf_submit(space, 0);
    // bpf_perf_event_output(ctx, &perf, BPF_F_CURRENT_CPU,mydata, sizeof(*mydata));
    bpf_map_delete_elem(&rq_map, &cpu);
    }
return 0;
}