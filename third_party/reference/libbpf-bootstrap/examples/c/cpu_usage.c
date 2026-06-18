#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <sys/resource.h>
#include "bpf/libbpf.h"
#include "cpu_usage.skel.h"

typedef unsigned long long u64;

struct process_t {
	int cpu_id;
	pid_t pid; // 进程的PID
	//u64 finish_t; //进程结束的时间
	u64 start_t; //进程开始的时间
	u64 used_t; //已经使用的CPU时间
	u64 total_t; //每个cpu占用的总时间
	u64 occ; //占用率
	u64 diff;
};
//现在想同时获取到这个进程在不同cpu下的占用率
static int libbpf_print_fn(enum libbpf_print_level level, const char *format, va_list args)
{
	return vfprintf(stderr, format, args);
	//使用 vfprintf 将格式化的消息打印到标准错误流（stderr）中
}

static volatile sig_atomic_t stop;

static void sig_int(int signo) //处理 SIGINT 信号, 以便在接收到中断信号时停止程序的执行
{
	stop = 1;
}

int handle_event(void *ctx, void *data, size_t data_sz)
{
	const struct process_t *md = data;
	printf(" cpu_id->%5u   pid->%5u   occupancy rate->%llu%%\n", md->cpu_id, md->pid, md->occ);
	return 0;
}

int main(int argc, char **argv)
{
	struct cpu_usage_bpf *skel;
	int err;

	/* Set up libbpf errors and debug info callback */
	libbpf_set_print(libbpf_print_fn); //在发生错误或需要输出调试信息时会调用指定的回调函数

	/* Open load and verify BPF application */
	skel = cpu_usage_bpf__open_and_load(); //加载和验证BPF应用程序
	if (!skel) {
		fprintf(stderr, "Failed to open BPF skeleton\n");
		return 1;
	}
	/* Attach tracepoint handler */
	err = cpu_usage_bpf__attach(skel); //参数代表了已加载和初始化的BPF程序对象
	if (err) { // 操作失败
		fprintf(stderr, "Failed to attach BPF skeleton\n");
		goto cleanup;
	}
	if (signal(SIGINT, sig_int) == SIG_ERR) {
		fprintf(stderr, "can't set signal handler: %s\n", strerror(errno));
		goto cleanup;
	}

	struct ring_buffer *pb = NULL;
	pb = ring_buffer__new(bpf_map__fd(skel->maps.rb),  handle_event,
			       NULL, NULL);
	if (!pb) {
		err = -1;
		fprintf(stderr, "Failed to create ring buffer\n");
		goto cleanup;
	}
	while (1) {
		err = ring_buffer__poll(pb, 100 /* timeout, ms */);
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
	ring_buffer__free(pb);
	cpu_usage_bpf__destroy(skel); //销毁之前创建的BPF程序对象 (skel), 确保释放相关资源
	return -err; //接下来, 它返回 -err 来表示错误的发生, 将错误码传递给调用方
}