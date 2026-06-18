#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>
#include "bpf/libbpf.h"
#include "merge_occupancy.skel.h"


#define TASK_COMM_LEN 16

#define u64 unsigned long long  int
const u64 INTERVAL = 5000000000; // 5秒对应的纳秒数
u64 mytime_st = 0; //计时器开始时间
u64 mytime_now = 0;
int flag = 0; //记录输出次数 0/奇数改now 偶数改st
int tid_cnt = 0, pid_cnt = 0;		// tid数组的数据个数，pid数组的数据个数

#define FSHIFT		11		/* nr of bits of precision */
#define FIXED_1		(1<<FSHIFT)	/* 1.0 as fixed-point */
#define LOAD_INT(x) ((x) >> FSHIFT)
#define LOAD_FRAC(x) LOAD_INT(((x) & (FIXED_1-1)) * 100)

#define SORTNUM  16
enum subsystem {
	IO,
	MEM,
	CPU,
};
enum index {
	SOME,
	FULL,
};

struct cpumask {
	long unsigned int bits[2];
};//从内核系统文件中找到定义然后创建

struct mmd //存储各进程信息
{
	bool kflag;
    //cpu_usage
    u64 tid;
	u64 cpu_id;
	u64 pid; // 进程的PID
	u64 start_t; //进程开始的时间
	u64 used_t; //已经使用的CPU时间
	double occ; //占用率
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
    uint64_t migration_pid;
    uint32_t migration_prevcpu;
    uint32_t migration_nextcpu;
    uint32_t migration_currentcpu;
    struct cpumask cpumask;
	//processlen
    int processlen_cpu;
    unsigned int processlen_count;  
};

struct my_data {
	struct mmd *my_data;
	int my_data_size;
} my_data_tid, my_data_pid;		// 统计内核态传递过来的数据

struct event {
	bool kflag;
    //cpu_usage
    u64 tid;
	u64 cpu_id;
	u64 pid; // 进程的PID
	u64 start_t; //进程开始的时间
	u64 used_t; //已经使用的CPU时间
	double occ; //占用率
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
    char runtime_comm[TASK_COMM_LEN];   
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
    uint64_t migration_pid;
    uint32_t migration_prevcpu;
    uint32_t migration_nextcpu;
    uint32_t migration_currentcpu;
    struct cpumask cpumask;
	//processlen
    int processlen_cpu;
    unsigned int processlen_count; 
};

struct psidata{
	int pid;
	char comm[TASK_COMM_LEN];	// 进程名称
	int sub;					//IO or MEMORY 子系统
	int indexnum;					//IO or MEMORY 子系统
	unsigned long avg_p[3];		//avg10 avg60 avg300
	u64 total_p;				//总压力
};


const char* get_subsystem_string(enum subsystem sub) {
    switch (sub) {
        case IO:
            return "IO";
        case MEM:
            return "MEM";
        case CPU:
            return "CPU";
        default:
            return "Unknown";
    }
}

const char* get_index_string(enum index idx) {
    switch (idx) {
        case SOME:
            return "SOME";
        case FULL:
            return "FULL";
        default:
            return "Unknown";
    }
}

int libbpf_print_fn(enum libbpf_print_level level, const char *format, va_list args)
{
	/* Ignore debug-level libbpf logs */
	if (level > LIBBPF_INFO)
		return 0;
	return vfprintf(stderr, format, args);
}

//取消内核内存限制
void bump_memlock_rlimit(void)
{
	struct rlimit rlim_new = { .rlim_cur = RLIM_INFINITY, .rlim_max = RLIM_INFINITY };
	//更新+错误处理
	if (setrlimit(RLIMIT_MEMLOCK, &rlim_new)) {
		fprintf(stderr, "Failed to relimit OS source");
		exit(1);
	}
}

//处理CTRL-C
static volatile bool exiting = false;

static void sig_handler(int sig)
{
	exiting = true;
}
//保存在文件里
void save_data_to_file(const struct event *data, int size)
{
	FILE *file;
	char filename[256];
	snprintf(filename, sizeof(filename), "D:/333333/cando/output.txt");

	file = fopen(filename, "a"); // 打开文件，"a" 表示以追加方式打开文件
	if (!file) {
		perror("Failed to open file");
		return;
	}

	for (int i = 0; i < size; i++) {
		fprintf(file, "TID: %lld, PID: %llu, Command: %s, Cputime: %.2lf\n", data[i].tid,
			data[i].pid, data[i].comm, data[i].occ);
	}

	fclose(file);
}

// 比较函数，用于qsort
int compare(const void *a, const void *b) {
    const struct mmd *mmd_a = (const struct mmd *)a;
    const struct mmd *mmd_b = (const struct mmd *)b;

    // 根据total_time字段进行降序排序
    if (mmd_a->used_t < mmd_b->used_t) return 1;
    if (mmd_a->used_t > mmd_b->used_t) return -1;
    return 0;
}

// 排序函数
void tid_sort(struct mmd data[], int cnt) {
    // 使用qsort进行排序
    qsort(data, cnt, sizeof(struct mmd), compare);
}


//按tid输出函数
void myprint_tid(struct mmd data[], int w) //p是时间差 用来计算占用率
{
	printf("CPU   TID          (PID)        Command            Cputime	    Shared 		Text		Data	  	Resident\n");
	for (int n = 0; n < SORTNUM; n++) {			
		if (data[n].pid != 0 && w != 0) {
			data[n].occ = data[n].used_t / ((double)w * 1000000000.0 / 100.0);   //!公式
			printf("%-3lld   %-10llu   %-10llu   %-16s   %-5.2lf%%   	     %lu   	       %lu   	       %lu		%lu\n", data[n].cpu_id, data[n].tid, data[n].pid,
			       data[n].comm, data[n].occ, data[n].shared, data[n].text, data[n].data, data[n].resident);
		}
	}
}

//按pid输出函数
void myprint_pid(struct mmd data[], int w) //p是时间差 用来计算占用率
{
	printf("CPU   PID          Command            Cputime	  Shared   	  Text	               Data	  	     Resident\n");
	for (int n = 0; n < SORTNUM; n++) {      
		if (data[n].pid != 0 && w != 0) {
			data[n].occ = data[n].used_t / ((double)w * 1000000000.0 / 100.0);   //!
			printf("%-3lld   %-10llu   %-16s   %-5.2lf%%       %lu      	  %lu			%lu			%lu\n", data[n].cpu_id, data[n].pid,
			       data[n].comm, data[n].occ, data[n].shared, data[n].text, data[n].data, data[n].resident);
		}
	}
}

void myprint_psi(const struct psidata *data){
	printf("pid=%-5d %-20s %-5s %-5s avg10=%2lu.%02lu  avg60=%2lu.%02lu  avg300=%2lu.%02lu  total=%llu \n",
        data->pid, data->comm, get_subsystem_string(data->sub), get_index_string(data->indexnum),
        LOAD_INT(data->avg_p[0]), LOAD_FRAC(data->avg_p[0]),
        LOAD_INT(data->avg_p[1]), LOAD_FRAC(data->avg_p[1]),
        LOAD_INT(data->avg_p[2]), LOAD_FRAC(data->avg_p[2]),
        data->total_p);
} 

void myprint_iolatency(const struct event *md){
	if(md->io_latency==0){
		printf("------------------IO request----------------\n");
	}else{
		printf("~~~~~~~~~~~~~~~~~~IO Complete~~~~~~~~~~~~~~~\n");
	}
    printf("[IO on] [%2d] pid->%-5d comm->%-3s  IOinsert_time->%-15llu IOsubmit_time->%-15llu IOcomplete_time->%-12llu [IOqueue_time]->%lu[IOexecution_time]->%lu [iolatency]->%lu ns\n", md->io_cpu, md->io_pid, md->io_comm, md->insert_time , md->issue_time, md->complete_time,md->queue_time, md->execution_time, md->io_latency);
} 

void myprint_tpruntime(const struct event *md){
	printf("CPU=%d comm=%s pid=%d  runtime=%llu [ns] vruntime=%llu [ns]  cpufreq=%d [KMhz]\n", md->runtime_cpu, md->runtime_comm, md->runtime_pid,
                md->runtime, md->vruntime,md->cur);
} 

void myprint_sched_dispatch(const struct event *md){
	printf("prev_pid->%8u  prev_prio->%5u prev_stata->%5lu   next_pid->%8u   next_prio->%5u \n",md->prev_pid,md->prev_prio,md->prev_state,md->next_pid,md->next_prio);
} 

void myprint_kprobe_delay(const struct event *md){
	printf("The pid:%-5llu latency-time is %5llu  ns   comm -> %-3s\n",  md->cpu_delay_pid, md->cpu_latency, 
						md->cpu_delay_comm);
} 

void myprint_buddy_stat(const struct event *md){
	printf("   Order:     0    1    2    3    4    5    6    7    8    9   10  \n");
    printf("Idle number:  %lu    %lu    %lu    %lu    %lu    %lu    %lu    %lu    %lu    %lu    %lu \n",md->num0,md->num1,md->num2,md->num3,md->num4,md->num5,md->num6,md->num7,md->num8,md->num9,md->num10);
    // printf(" Timestamp: %-12lu \n ", md->alloc_time );
} 

void myprint_cpu_migration(const struct event *user_data){
	printf("PID：%lu，原始 CPU：%u，目标 CPU：%u，当前 CPU：%u\n",
       user_data->migration_pid, user_data->migration_prevcpu, user_data->migration_nextcpu, user_data->migration_currentcpu);
    printf("CPU Mask: %lx %lx\n", user_data->cpumask.bits[0], user_data->cpumask.bits[1]);
}

void myprint_processlen(const struct event *md){
	printf(" cpu: %d count:%d\n ",  md->processlen_cpu, md->processlen_count);
} 

void perfbuf_handle_event(void *ctx, int cpu, void *data, unsigned int data_sz)
{
	const struct psidata *e = data;

    myprint_psi(e);
}

//回调处理函数
int ringbuf_event_handler(void *ctx, void *data, size_t data_sz)
{
	const struct event *md = data;

	if(md->kflag ==false){//判断后还没有卡顿
		return 0;
	}
	int tid_val, pid_val;
	if (!strncmp(md->comm, "swapper", 7)) {
		return 0;
	}
	
	for (tid_val = 0; tid_val < tid_cnt; ++tid_val) {
		if (md->pid == my_data_tid.my_data[tid_val].pid && md->tid == my_data_tid.my_data[tid_val].tid) {
			break;
		}
	}
	for (pid_val = 0; pid_val < pid_cnt; ++pid_val) {
		if (md->pid == my_data_pid.my_data[pid_val].pid) {
			break;
		}
	}

	if (tid_val != tid_cnt) {
		my_data_tid.my_data[tid_val].used_t += md->used_t;
		my_data_tid.my_data[tid_val].cpu_id = md->cpu_id;
		if(my_data_tid.my_data[tid_val].pid == my_data_tid.my_data[tid_cnt].pid){
			my_data_tid.my_data[tid_val].resident += md->resident;
		}
	} else {
		my_data_tid.my_data[tid_cnt].cpu_id = md->cpu_id;
		my_data_tid.my_data[tid_cnt].pid = md->pid;
		my_data_tid.my_data[tid_cnt].tid = md->tid;
		my_data_tid.my_data[tid_cnt].used_t = md->used_t;
		strcpy(my_data_tid.my_data[tid_cnt].comm, md->comm);
		my_data_tid.my_data[tid_cnt].shared = md->shared;
		my_data_tid.my_data[tid_cnt].data = md->data;
		my_data_tid.my_data[tid_cnt].text = md->text;
		my_data_tid.my_data[tid_cnt].resident = md->resident;
		
	}

	if (pid_val != pid_cnt) {
		my_data_pid.my_data[pid_val].used_t += md->used_t;
		my_data_pid.my_data[pid_val].cpu_id = md->cpu_id;
	} else {
		my_data_pid.my_data[pid_cnt].cpu_id = md->cpu_id;
		my_data_pid.my_data[pid_cnt].pid = md->pid;
		my_data_pid.my_data[pid_cnt].tid = md->tid;
		my_data_pid.my_data[pid_cnt].used_t = md->used_t;
		strcpy(my_data_pid.my_data[pid_cnt].comm, md->comm);
		my_data_pid.my_data[pid_cnt].shared= md->shared;
		my_data_pid.my_data[pid_cnt].data = md->data;
		my_data_pid.my_data[pid_cnt].text = md->text;
		my_data_pid.my_data[pid_cnt].resident = md->resident;
	}


		time_t timep; //1970年到当前的秒数
		mytime_now = time(&timep); //获取当前时间戳
		u64 time_de = mytime_now - mytime_st;
		if (time_de >= 5) {
		tid_sort(my_data_tid.my_data, tid_cnt);
		myprint_tid(my_data_tid.my_data, time_de);
		tid_sort(my_data_pid.my_data, pid_cnt);
		myprint_pid(my_data_pid.my_data, time_de);
		flag = 1;
		mytime_st = time(&timep); //修改起始时间，重新开始计时
		time_de = 0;
		tid_cnt = 0;
		pid_cnt = 0;
		memset(my_data_tid.my_data, 0, sizeof(struct mmd) * my_data_tid.my_data_size);
		memset(my_data_pid.my_data, 0, sizeof(struct mmd) * my_data_pid.my_data_size);
	} else {
		if (tid_val == tid_cnt) {
			tid_cnt++;
		}
		if (pid_val == pid_cnt) {
			pid_cnt++;
		}
		flag = 0;
	}
	if (tid_cnt == my_data_tid.my_data_size) {
		my_data_tid.my_data_size <<= 1;
		my_data_tid.my_data = (struct mmd *)realloc(my_data_tid.my_data, sizeof(struct mmd) * my_data_tid.my_data_size);//!这里的乘法？
	}
	if (pid_cnt == my_data_pid.my_data_size) {
		my_data_pid.my_data_size <<= 1;
		my_data_pid.my_data = (struct mmd *)realloc(my_data_pid.my_data, sizeof(struct mmd) * my_data_pid.my_data_size);
	}
		
	return 0;
}

//主逻辑
int main(int argc, char **argv)
{
	struct ring_buffer *rb = NULL;
	//通过${apps}.skel.h操作控制交互内核态程序
	struct merge_occupancy_bpf *skel;
	int err;
	//logs
	libbpf_set_print(libbpf_print_fn);
	//os settings
	bump_memlock_rlimit();
	//Clean handling
	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	//---------------------------------------------------------------
	//加载，验证ebpf内核态程序
	skel = merge_occupancy_bpf__open_and_load();
	if (!skel) {
		fprintf(stderr, "failed to open and verify BPF skeleton");
		return -1;
	}

	//挂载
	err = merge_occupancy_bpf__attach(skel);
	if (err) {
		fprintf(stderr, "Failed to attach BPF skeleton");
		goto cleanup; //良好习惯清理资源
	}

	/*-----------------------perfbuffer操作部分--------------------*/
	struct perf_buffer *pb = NULL;
	pb = perf_buffer__new(bpf_map__fd(skel->maps.pb), 8 /* 32KB per CPU */, perfbuf_handle_event, NULL, NULL, NULL);
	if(libbpf_get_error(pb)) {
		err = -1;
		fprintf(stderr, "Failed to create perf buffer\n");
		goto cleanup;
	}

	/*-----------------------ringbuffer操作部分--------------------*/
	rb = ring_buffer__new(bpf_map__fd(skel->maps.rb), ringbuf_event_handler, NULL, NULL);
	if (!rb) {
		err = -1;
		fprintf(stderr, "Failed to create ring buffer\n");
		goto cleanup;
	}

	

	my_data_tid.my_data_size = 200;
	my_data_tid.my_data = (struct mmd *)malloc(sizeof(struct mmd) * my_data_tid.my_data_size);
	memset(my_data_tid.my_data, 0, sizeof(struct mmd) * my_data_tid.my_data_size);

	my_data_pid.my_data_size = 200;
	my_data_pid.my_data = (struct mmd *)malloc(sizeof(struct mmd) * my_data_pid.my_data_size);
	memset(my_data_pid.my_data, 0, sizeof(struct mmd) * my_data_pid.my_data_size);

	while (!exiting) {
		err = ring_buffer__poll(rb, -1);
		if (err == -EINTR) {
			err = 0;
			break;
		}
		if (err < 0) {
			printf("Error polling ring buffer: %d\n", err);
			break;
		}
		err = perf_buffer__poll(pb, 100 /* timeout, ms */);
		/* Ctrl-C will cause -EINTR */
		if (err == -EINTR) {
			err = 0;
			break;
		}
		if (err < 0) {
			printf("Error polling perf buffer: %d\n", err);
			break;
		}
	}

cleanup:
	free(my_data_tid.my_data);
	free(my_data_pid.my_data);
	ring_buffer__free(rb);
	perf_buffer__free(pb);
	merge_occupancy_bpf__destroy(skel);

	return err < 0 ? -err : 0;
}