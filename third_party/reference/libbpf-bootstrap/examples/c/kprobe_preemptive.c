#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <sys/resource.h>
#include "bpf/libbpf.h"	
#include "kprobe_preemptive.skel.h"

typedef unsigned long long u64;
typedef unsigned long u32;
#define TASK_COMM_LEN	 16

struct mydata_t{			//接收到的数据
	u64 prev_pid;    
    u64 time;  
    u64 next_pid; 
	char comm[TASK_COMM_LEN];  
};

static int libbpf_print_fn(enum libbpf_print_level level, const char *format, va_list args)
{
	return vfprintf(stderr, format, args);
	//使用 vfprintf 将格式化的消息打印到标准错误流（stderr）中
}

static volatile sig_atomic_t stop;

static void sig_int(int signo)				//处理 SIGINT 信号, 以便在接收到中断信号时停止程序的执行
{
	stop = 1;
}

void get_time_str_ms(char *time_str, int len)
{
	struct timeval tv;
	struct timezone tz;
	struct tm *p;
	
	memset(time_str, 0, len);
	
	gettimeofday(&tv,&tz);

	p = localtime(&tv.tv_sec);

	sprintf(time_str,"%d-%02d-%02d %02d:%02d:%02d.%03ld",
		(1900+p->tm_year),(1+p->tm_mon),p->tm_mday,(p->tm_hour),p->tm_min,p->tm_sec,tv.tv_usec/1000);
}

void handle_event(void *ctx, int cpu, void *data, unsigned int data_sz)
{
    const struct mydata_t md = *(struct mydata_t *)data;
	char str[30];
    get_time_str_ms(str, sizeof(str));
    printf("[%-16s]  Process %10llu  |  preempted by Process %10llu  |  at timestamp [%10s]\n", md.comm, md.prev_pid, 
                md.next_pid, str);
}

int main(int argc, char **argv)
{
	struct kprobe_preemptive_bpf *skel;
	int err;

	/* Set up libbpf errors and debug info callback */
	libbpf_set_print(libbpf_print_fn);				//在发生错误或需要输出调试信息时会调用指定的回调函数

	/* Open load and verify BPF application */
	skel = kprobe_preemptive_bpf__open_and_load();				//加载和验证BPF应用程序
	if (!skel) {
		fprintf(stderr, "Failed to open BPF skeleton\n");
		return 1;
	}
	/* Attach tracepoint handler */
	err = kprobe_preemptive_bpf__attach(skel);		//参数代表了已加载和初始化的BPF程序对象
	if (err) {								// 操作失败
		fprintf(stderr, "Failed to attach BPF skeleton\n");
		goto cleanup;
	}
	if (signal(SIGINT, sig_int) == SIG_ERR) {
		fprintf(stderr, "can't set signal handler: %s\n", strerror(errno));
		goto cleanup;
	}

	struct perf_buffer *pb = NULL;
	pb = perf_buffer__new(bpf_map__fd(skel->maps.perf), 8 /* 32KB per CPU */, handle_event, NULL, NULL, NULL);
	if(libbpf_get_error(pb)) {
		err = -1;
		fprintf(stderr, "Failed to create perf buffer\n");
		goto cleanup;
	}
	// int a = 1;
    while(1){
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
	kprobe_preemptive_bpf__destroy(skel);			//销毁之前创建的BPF程序对象 (skel), 确保释放相关资源
	return -err;				//接下来, 它返回 -err 来表示错误的发生, 将错误码传递给调用方
}