#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>
#include "bpf/libbpf.h"
#include <pthread.h>
#include "merge_latency.skel.h"

#ifndef TASK_COMM_LEN
	#define TASK_COMM_LEN 16
#endif
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

enum subsystem {
	IO,
	MEM,
	CPU,
};
enum index {
	SOME,
	FULL,
};

struct mmd //存储各进程信息
{
	u64 tid;
	u64 cpu_id;
	u64 pid; // 进程的PID
	u64 start_t; //进程开始的时间
	u64 used_t; //已经使用的CPU时间
	double occ; //占用率
	char comm[TASK_COMM_LEN];
	u64 task_tgid;
    u64 task_pid;
	long unsigned int total_vm;
    long unsigned int data;
    long unsigned int text;
    long unsigned int shared;
    long unsigned int resident;
    // int psi_pid;
	// char psi_comm[TASK_COMM_LEN];	// 进程名称
	// int sub;					//IO or MEMORY 子系统
	// int indexnum;					//IO or MEMORY 子系统
	// char subsystem[10];			//IO or MEMORY 子系统
	// char index[5];              //some or full 指标
	// unsigned long avg_p[3];		//avg10 avg60 avg300
	// u64 total_p;
};

struct my_data {
	struct mmd *my_data;
	int my_data_size;
} my_data_tid, my_data_pid;		// 统计内核态传递过来的数据

struct event {
	u64 tid;
	u64 cpu_id;
	u64 pid;
	u64 start_t;
	u64 used_t; //已经使用的CPU时间
	double occ;
	char comm[TASK_COMM_LEN];
	u64 task_tgid;
    u64 task_pid;
	long unsigned int total_vm;
    long unsigned int data;
    long unsigned int text;
    long unsigned int shared;
    long unsigned int resident;
    // int psi_pid;
	// char psi_comm[TASK_COMM_LEN];	// 进程名称
	// int sub;					//IO or MEMORY 子系统
	// int indexnum;					//IO or MEMORY 子系统
	// char subsystem[10];			//IO or MEMORY 子系统
	// char index[5];              //some or full 指标
	// unsigned long avg_p[3];		//avg10 avg60 avg300
	// u64 total_p;
};

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
void myprint_tid(struct mmd data[], int w) //w是时间差 用来计算占用率
{
	printf("CPU   TID          (PID)        Command            Cputime	    Shared 		Text		Data	  	Resident\n");
	for (int n = 0; n < 16; n++) {   //! 16改成宏
		if (data[n].pid != 0 && w != 0) {
			data[n].occ = data[n].used_t / ((double)w * 1000000000.0 / 100.0);  //! 数字改成宏 ,计算公式
			printf("%-3lld   %-10llu   %-10llu   %-16s   %-5.2lf%%   	     %lu   	       %lu   	       %lu		%lu\n", data[n].cpu_id, data[n].tid, data[n].pid,
			       data[n].comm, data[n].occ, data[n].shared, data[n].text, data[n].data, data[n].resident);
		}
	}
}

//按pid输出函数
void myprint_pid(struct mmd data[], int w) //w是时间差 用来计算占用率
{
	printf("CPU   PID          Command            Cputime	  Shared   	  Text	               Data	  	     Resident\n");
	for (int n = 0; n < 16; n++) {//! 16改成宏
		if (data[n].pid != 0 && w != 0) {
			data[n].occ = data[n].used_t / ((double)w * 1000000000.0 / 100.0);//! 数字改成宏
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

void perfbuf_handle_event(void *ctx, int cpu, void *data, unsigned int data_sz)
{
	const struct psidata *e = data;
	if(e->flag == 0){
		myprint_psi(e);
	}else{
		return;
	}

}

//回调处理函数
int ringbuf_event_handler(void *ctx, void *data, size_t data_sz)
{
	const struct event *md = data;
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

	//my_data[i].resident = md->resident;
	// printf("%10llu	(%5llu) 	%20s	%5lld\n", my_data.my_data[i].tid, my_data.my_data[i].pid,
	//        my_data.my_data[i].comm, my_data.my_data[i].used_t);
	    // struct event *psi_data = data;
		// myprint_psi(psi_data );
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
		my_data_tid.my_data = (struct mmd *)realloc(my_data_tid.my_data, sizeof(struct mmd) * my_data_tid.my_data_size);
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
	struct merge_latency_bpf *skel;
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
	skel = merge_latency_bpf__open_and_load();
	if (!skel) {
		fprintf(stderr, "failed to open and verify BPF skeleton");
		return -1;
	}

	//挂载
	err = merge_latency_bpf__attach(skel);
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

	//int a = 10;
	while (!exiting) {
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
		err = ring_buffer__poll(rb, -1);
		if (err == -EINTR) {
			err = 0;
			break;
		}
		if (err < 0) {
			printf("Error polling ring buffer: %d\n", err);
			break;
		}

		//usleep(500000);           //!
	}

cleanup:
	free(my_data_tid.my_data);
	free(my_data_pid.my_data);
	ring_buffer__free(rb);
	perf_buffer__free(pb);
	merge_latency_bpf__destroy(skel);

	return err < 0 ? -err : 0;
}
