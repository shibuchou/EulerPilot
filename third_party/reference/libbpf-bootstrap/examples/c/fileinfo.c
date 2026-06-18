// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
/* Copyright (c) 2021 Sartura
 * Based on minimal.c by Facebook */
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <sys/resource.h>
#include <bpf/libbpf.h>
#include "fileinfo.skel.h"
#include <argp.h>
typedef unsigned long long u64;
#define MAX_PIDS 5
struct fileinfo{
	u64 opentimestamp;
	u64 time;
	pid_t pid;
	char appname[128];
	char filename[256];
	u64 fd;
};


static int libbpf_print_fn(enum libbpf_print_level level, const char *format, va_list args)
{
	return vfprintf(stderr, format, args);
}

static volatile sig_atomic_t stop;

static void sig_int(int signo)
{
	stop = 1;
}

void handle_event(void *ctx, int cpu, void *data, unsigned int data_sz)
{
    const struct fileinfo *md = data;
	
    printf("%-8d %-6s  %s\n", md->pid, md->appname, md->filename);
}
// argp 配置
const char *argp_program_version = "appopeninfo 1.0";
const char *argp_program_bug_address = "<1285719445@qq.com>";
const char argp_program_doc[] =
	"BPF syscall application.\n"
	"\n"
	"It traces the initiation and termination of process system calls \n"
	"You can filter the pid and modify the trigger threshold to display it at an appropriate time. \n"
	"The information it outputs includes  PID command syscall_number, syscall_name and syscall runtime\n"
	"USAGE: ./syscall [-p <pid>] [-s 'some/full xxx' ] [-t time_s] [-v]\n";

// 命令行选项
// 选项的长名称，“-name”形式		选项的短名称的 ASCII 码		如果非 0，为选项的参数名
// 如果非 0，为选项的标志。OPTION_ALIAS 表示当前选项是前一个选项的别名			选项说明
static const struct argp_option opts[] = {
	{ "verbose", 'v', NULL, 0, "Verbose debug output" },
	{ "pid", 'p', "pid", 0, "Monitor system calls of the specified PID process." },	
    {"app",'a',"app",0,"app name"},
	{}
};
static struct env {
	bool verbose; // 是否启用详细的调试输出
	int pid[10]; // 指定的pid
	char app[256];
} env;


// argp 解析器回调
// key 参数：表示当前解析到的选项的键值。根据 switch 语句中的不同情况，对不同的选项进行处理。对于短选项（例如 -v），它是字符的 ASCII 值（在这种情况下，'v'）。对于长选项（例如 --verbose），它是在 argp_option 结构体中为这个选项指定的值。还有一些预定义的键值（例如 ARGP_KEY_ARG 或 ARGP_KEY_END），它们表示特定的解析事件而不是特定的选项。
// arg 参数：如果选项带有参数，这个参数表示选项的参数值。处理 -v -p -s 选项，如果当前处理的参数没有关联值，arg 将为 NULL。
// state 参数：是 argp 解析状态的指针，用于与整个解析过程进行交互。包含了解析过程的一些信息，如程序名、argp 结构等。
static error_t parse_arg(int key, char *arg, struct argp_state *state)
{
	switch (key) {
	case 'v':
		env.verbose = true; // 启用verbose
		break;
	case 'p':
		errno = 0;
		env.pid[0] = strtol(arg, NULL, 10);
		if (errno || env.pid <= 0) { // 无效pid
			// 处理无效的pid参数
			fprintf(stderr, "Invalid duration: %s\n", arg);
			// 用于输出关于如何使用程序的信息，然后退出程序。
			// state: 指向一个 argp_state 结构的指针，该结构包含了 argp 的解析状态。这通常是在 argp 的解析函数中被传入的。
			argp_usage(state); // 显示用法信息并退出程序
		}
		break;
	case 'a':
		strcpy(env.app, arg);

		FILE *fp;
		char buffer[1024];
		char command[1024];
		int pids[MAX_PIDS];
		int count = 0;
		snprintf(command, sizeof(command),"ps -ef -o pid,name | grep %s | awk '{print $1}'", env.app);
		fp = popen(command, "r"); // 执行Shell命令并打开管道读取输出

		if (fp == NULL) {
			printf("Failed to execute the command.\n");
			return 1;
		}

		// 保存输出的PID到数组中
		while (fgets(buffer, sizeof(buffer), fp) != NULL) {
			int pid = atoi(buffer);
			
			pids[count] = pid;
			env.pid[count] = pids[count];
			count++;
			if (count >= MAX_PIDS) {
				break;
			}
			
		}
		

		pclose(fp); // 关闭管道

		break;	
	case ARGP_KEY_ARG:
		argp_usage(state); // 处理未知的额外参数，显示用法信息并退出程序
		break;
	default:
		return ARGP_ERR_UNKNOWN; // 处理未知的选项，返回未知错误码
	}
	return 0;
}
// argp 结构
static const struct argp argp = {
	.options = opts, // 指定要解析的选项，为 0 时没有选项
	.parser = parse_arg, // 解析选项的函数，为 0 时不解析选项
	.doc = argp_program_doc, // 程序说明，为 0 时没有程序说明
};

int main(int argc, char **argv)
{
	struct fileinfo_bpf *skel;
	int err;
	err = argp_parse(&argp, argc, argv, 0, NULL, NULL);
	if (err)
		return err;

	/* Set up libbpf errors and debug info callback */
	libbpf_set_print(libbpf_print_fn);

	//open和load分离
	// 打开BPF程序，创建BPF程序的上下文
	skel = fileinfo_bpf__open();
	if (!skel) {
		fprintf(stderr, "Failed to open BPF skeleton\n");
		return 1;
	}

	// 确保BPF程序仅处理来自我们指定进程的系统调用

	
		skel->bss->pid_target_0 = env.pid[0];
		skel->bss->pid_target_1 = env.pid[1];
		skel->bss->pid_target_2 = env.pid[2];
		skel->bss->pid_target_3 = env.pid[3];
		skel->bss->pid_target_4 = env.pid[4];

	// 加载并验证BPF程序
	err = fileinfo_bpf__load(skel);
	if (err) {
		fprintf(stderr, "Failed to load and verify BPF skeleton\n");
		goto cleanup;
	}


	/* Attach tracepoint handler */
	err = fileinfo_bpf__attach(skel);
	if (err) {
		fprintf(stderr, "Failed to attach BPF skeleton\n");
		goto cleanup;
	}

	if (signal(SIGINT, sig_int) == SIG_ERR) {
		fprintf(stderr, "can't set signal handler: %s\n", strerror(errno));
		goto cleanup;
	}

	printf("Successfully started! Please run `sudo cat /sys/kernel/debug/tracing/trace_pipe` "
	       "to see output of the BPF programs.\n");



	for(int i=0;i<MAX_PIDS;i++)
	{
		if(env.pid[i]==0) break;
		printf("it's process pid: %d  \n",env.pid[i]);
	}
	printf("%-8s %-5s  %-16s\n",
	        "PID", "COMM", "FILENAME");


	struct perf_buffer *pb = NULL;
	pb = perf_buffer__new(bpf_map__fd(skel->maps.perf), 8 /* 32KB per CPU */, handle_event, NULL, NULL, NULL);
	if(libbpf_get_error(pb)) {
		err = -1;
		fprintf(stderr, "Failed to create perf buffer\n");
		goto cleanup;
	}

	
    while(!stop){
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
	fileinfo_bpf__destroy(skel);
	return -err;
}
