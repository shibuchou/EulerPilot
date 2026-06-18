#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>
#include "bpf/libbpf.h"
#include "merge_latency_lessfor.skel.h"
#include "../perfinsight/perfinsight.h"

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
};

struct my_data {
	struct mmd *my_data;
	int my_data_size;
} my_data_tid, my_data_pid;		// 统计内核态传递过来的数据

struct event {
	bool kflag;
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

};

struct psidata{
	int pid;
	char comm[TASK_COMM_LEN];	// 进程名称
	int sub;					//IO or MEMORY 子系统
	int indexnum;					//IO or MEMORY 子系统
	unsigned long avg_p[3];		//avg10 avg60 avg300
	u64 total_p;				//总压力
};


// 全局变量参数功能控制
static int threshold = 0;
static int flagSave2file = 0;
static int psi_resource = MEM;
static int psi_window = 0;
static int psi_index = SOME;
static int output_interval_sec = 5;
static int current_psi_trigger = 0;
int psi_handle_flag = 0;
int trigger_flag = 0;


static FILE* open_log_file(const char* log_file_path) {
    FILE* log_file = fopen(log_file_path, "a"); // 以追加模式打开文件，如果不存在则创建
    if (log_file == NULL) {
        perror("Failed to open log file");
        exit(EXIT_FAILURE);
    }
    return log_file;
}


const static char* get_subsystem_string(enum subsystem sub) {
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

const static char* get_index_string(enum index idx) {
    switch (idx) {
        case SOME:
            return "SOME";
        case FULL:
            return "FULL";
        default:
            return "Unknown";
    }
}

static int parse_resource_arg(const char *value)
{
	if (!value) {
		return MEM;
	}
	if (strcmp(value, "io") == 0) {
		return IO;
	}
	if (strcmp(value, "mem") == 0) {
		return MEM;
	}
	if (strcmp(value, "cpu") == 0) {
		return CPU;
	}
	if (strcmp(value, "any") == 0) {
		return 3;
	}
	return MEM;
}

static int parse_window_arg(const char *value)
{
	if (!value) {
		return 0;
	}
	if (strcmp(value, "avg60") == 0) {
		return 1;
	}
	if (strcmp(value, "avg300") == 0) {
		return 2;
	}
	return 0;
}

static int parse_index_arg(const char *value)
{
	if (!value) {
		return SOME;
	}
	if (strcmp(value, "full") == 0) {
		return FULL;
	}
	return SOME;
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
		fprintf(stderr, "Warning: failed to raise RLIMIT_MEMLOCK, continue anyway\n");
	}
}

//处理CTRL-C
static volatile bool exiting = false;

static void sig_handler(int sig)
{
	exiting = true;
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

	if(flagSave2file){
		FILE* log_file = open_log_file("/data/local/tmp/perfinsight/log/merge_latency.log");
		
		fprintf(log_file, "CPU   TID          (PID)        Command            Cputime	    Shared 		Text		Data	  	Resident\n");
		for (int n = 0; n < SORTNUM; n++) {			
			if (data[n].pid != 0 && w != 0) {
					data[n].occ = data[n].used_t / ((double)w * 1000000000.0 / 100.0);   //!公式
					fprintf(log_file, "%-3lld   %-10llu   %-10llu   %-16s   %-5.2lf%%   	     %lu   	       %lu   	       %lu		%lu\n", data[n].cpu_id, data[n].tid, data[n].pid,
						data[n].comm, data[n].occ, data[n].shared, data[n].text, data[n].data, data[n].resident);
				}
		}

		// 关闭日志文件
		fclose(log_file);
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

	if(flagSave2file){
		FILE* log_file = open_log_file("/data/local/tmp/perfinsight/log/merge_latency.log");
		
		fprintf(log_file, "CPU   PID          Command            Cputime	  Shared   	  Text	               Data	  	     Resident\n");
			for (int n = 0; n < SORTNUM; n++) {      
				if (data[n].pid != 0 && w != 0) {
					data[n].occ = data[n].used_t / ((double)w * 1000000000.0 / 100.0);   //!
					fprintf(log_file, "%-3lld   %-10llu   %-16s   %-5.2lf%%       %lu      	  %lu			%lu			%lu\n", data[n].cpu_id, data[n].pid,
						data[n].comm, data[n].occ, data[n].shared, data[n].text, data[n].data, data[n].resident);
				}
			}
		// 关闭日志文件
		fclose(log_file);
	}
}

void myprint_psi(const struct psidata *data){

	printf("pid=%-5d %-20s %-5s %-5s avg10=%2lu.%02lu  avg60=%2lu.%02lu  avg300=%2lu.%02lu  total=%llu \n",
        data->pid, data->comm, get_subsystem_string(data->sub), get_index_string(data->indexnum),
        LOAD_INT(data->avg_p[0]), LOAD_FRAC(data->avg_p[0]),
        LOAD_INT(data->avg_p[1]), LOAD_FRAC(data->avg_p[1]),
        LOAD_INT(data->avg_p[2]), LOAD_FRAC(data->avg_p[2]),
        data->total_p);

	//判断是否输出到文件
	if(flagSave2file){
		FILE* log_file = open_log_file("/data/local/tmp/perfinsight/log/merge_latency.log");
		
		fprintf(log_file, "pid=%-5d %-20s %-5s %-5s avg10=%2lu.%02lu  avg60=%2lu.%02lu  avg300=%2lu.%02lu  total=%llu \n",
            data->pid, data->comm, get_subsystem_string(data->sub), get_index_string(data->indexnum),
            LOAD_INT(data->avg_p[0]), LOAD_FRAC(data->avg_p[0]),
            LOAD_INT(data->avg_p[1]), LOAD_FRAC(data->avg_p[1]),
            LOAD_INT(data->avg_p[2]), LOAD_FRAC(data->avg_p[2]),
            data->total_p);

		// 关闭日志文件
		fclose(log_file);
	}
} 

static void perfbuf_handle_event(void *ctx, int cpu, void *data, unsigned int data_sz)
{
	const struct psidata *e = data;
	unsigned long avg = e->avg_p[psi_window];

	if (psi_resource == 3) {
		if (e->indexnum == psi_index && e->sub == IO) {
			current_psi_trigger = 0;
		}
		if (e->indexnum == psi_index && LOAD_INT(avg) >= threshold) {
			current_psi_trigger = 1;
		}
		if (e->sub == CPU) {
			trigger_flag = current_psi_trigger;
		}
	} else if (e->sub == psi_resource && e->indexnum == psi_index) {
		trigger_flag = LOAD_INT(avg) >= threshold;
	}

    myprint_psi(e);
}

//回调处理函数
static int ringbuf_event_handler(void *ctx, void *data, size_t data_sz)
{
	const struct event *md = data;

	if(md->kflag ==false){//判断后还没有卡顿
		trigger_flag = 0;
		return 0;
	}
	trigger_flag = 1;
	
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
		if (time_de >= output_interval_sec) {
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
void target_merge_latency(void *arg)
{
	char **argv = (char **)arg;
    

    // Parse argv to find -threshold parameter
    int length = 0;
    while (argv[length] != NULL) {
        length++;
    }
#ifdef DEBUG
	printf("[DEBUG] argv have :%d\n", length);
#endif
    for (int i = 0; i < length; i++) {
		if(argv[i] == NULL){
			break;
		}

		if (strcmp(argv[i], "-threshold") == 0 && argv[i + 1] != NULL) {
            threshold = atoi(argv[i + 1]);
#ifdef DEBUG
            printf("[DEBUG] threshold has been given an value! %d\n", threshold);
#endif
			
        }
		if (strcmp(argv[i], "-trigger") == 0) {
            psi_handle_flag = 1;
#ifdef DEBUG
            printf("[DEBUG] trigger has been set\n");
#endif
        }

		if (strcmp(argv[i], "-save") == 0) {
            flagSave2file = 1;
#ifdef DEBUG
            printf("[DEBUG] save has been set\n");
#endif
        }
		if (strcmp(argv[i], "-resource") == 0 && argv[i + 1] != NULL) {
			psi_resource = parse_resource_arg(argv[i + 1]);
		}
		if (strcmp(argv[i], "-window") == 0 && argv[i + 1] != NULL) {
			psi_window = parse_window_arg(argv[i + 1]);
		}
		if (strcmp(argv[i], "-index") == 0 && argv[i + 1] != NULL) {
			psi_index = parse_index_arg(argv[i + 1]);
		}
		if (strcmp(argv[i], "-interval") == 0 && argv[i + 1] != NULL) {
			output_interval_sec = atoi(argv[i + 1]);
			if (output_interval_sec <= 0) {
				output_interval_sec = 5;
			}
		}
#ifdef DEBUG
        printf("[Inner Parse] argv[%d]: %s\n", i, argv[i]);
#endif
    }

    if (threshold == 0) {
        printf("Threshold not specified or invalid \n");
    }
	if (psi_handle_flag == 0) {
        printf("PSI trigger others didn't set \n");
    }
#ifdef DEBUG
	printf("[DEBUG] threshold: %d\n", threshold);
#endif

	
	struct ring_buffer *rb = NULL;
	//通过${apps}.skel.h操作控制交互内核态程序
	struct merge_latency_lessfor_bpf *skel;
	int err;
	//logs
	libbpf_set_print(libbpf_print_fn);
	//os settings
	bump_memlock_rlimit();
	//Clean handling
	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

#ifdef DEBUG    
    printf("[DEBUG] AAA\n");
#endif 
	//---------------------------------------------------------------
	//加载，验证ebpf内核态程序
	skel = merge_latency_lessfor_bpf__open();
	if (!skel) {
		fprintf(stderr, "failed to open  BPF skeleton");
		return -1;
	}
#ifdef DEBUG    
    printf("[DEBUG] BBB\n");
#endif 
	skel->bss->threshold = threshold;
	skel->bss->trigger_resource = psi_resource;
	skel->bss->trigger_window = psi_window;
	skel->bss->trigger_index = psi_index;

#ifdef DEBUG    
    printf("[DEBUG] CCC\n");
#endif 
	err = merge_latency_lessfor_bpf__load(skel);
	if (err) {
		fprintf(stderr, "Failed to load and verify BPF skeleton\n");
		goto cleanup;
	}
	//挂载
	err = merge_latency_lessfor_bpf__attach(skel);
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
		err = ring_buffer__poll(rb, 100);
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
	merge_latency_lessfor_bpf__destroy(skel);

	return err < 0 ? -err : 0;
}

