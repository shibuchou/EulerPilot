// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
/* Copyright (c) 2021 Sartura
 * Based on minimal.c by Facebook */
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <sys/resource.h>
#include "bpf/libbpf.h"
#include "psi_pf.skel.h"
#include "../perfinsight/perfinsight.h"


#ifndef TASK_COMM_LEN
	#define TASK_COMM_LEN 16
#endif

struct psidata{
	int pid;
	char comm[TASK_COMM_LEN];	// 进程名称
	int sub;					//IO or MEMORY 子系统
	int indexnum;					//IO or MEMORY 子系统
	char subsystem[10];			//IO or MEMORY 子系统
	char index[5];              //some or full 指标
	unsigned long avg_p[3];		//avg10 avg60 avg300
	__u64 total_p;				//总压力
};

enum subsystem {
	IO,
	MEM,
	CPU,
};
enum index {
	SOME,
	FULL,
};


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



#define FSHIFT		11		/* nr of bits of precision */
#define FIXED_1		(1<<FSHIFT)	/* 1.0 as fixed-point */
#define LOAD_INT(x) ((x) >> FSHIFT)
#define LOAD_FRAC(x) LOAD_INT(((x) & (FIXED_1-1)) * 100)



static int libbpf_print_fn(enum libbpf_print_level level, const char *format, va_list args)
{
	return vfprintf(stderr, format, args);
}

static volatile sig_atomic_t stop;

static void sig_int(int signo)
{
	stop = 1;
}

static void handle_event(void *ctx, int cpu, void *data, unsigned int data_sz)
{
	if(psi_handle_flag){
		if(trigger_flag){
			const struct psidata *e = data;

			printf("pid=%-5d %-20s %-5s %-5s avg10=%2lu.%02lu  avg60=%2lu.%02lu  avg300=%2lu.%02lu  total=%llu\n",
				e->pid, e->comm, get_subsystem_string(e->sub), get_index_string(e->indexnum),
				LOAD_INT(e->avg_p[0]), LOAD_FRAC(e->avg_p[0]),
				LOAD_INT(e->avg_p[1]), LOAD_FRAC(e->avg_p[1]),
				LOAD_INT(e->avg_p[2]), LOAD_FRAC(e->avg_p[2]),
				e->total_p);
		}
		return;
	}else{
		const struct psidata *e = data;

		printf("pid=%-5d %-20s %-5s %-5s avg10=%2lu.%02lu  avg60=%2lu.%02lu  avg300=%2lu.%02lu  total=%llu\n",
			e->pid, e->comm, get_subsystem_string(e->sub), get_index_string(e->indexnum),
			LOAD_INT(e->avg_p[0]), LOAD_FRAC(e->avg_p[0]),
			LOAD_INT(e->avg_p[1]), LOAD_FRAC(e->avg_p[1]),
			LOAD_INT(e->avg_p[2]), LOAD_FRAC(e->avg_p[2]),
			e->total_p);
		}

}


void target_psi_pf()
{
	struct psi_pf_bpf *skel;
	int err;

	/* Set up libbpf errors and debug info callback */
	libbpf_set_print(libbpf_print_fn);

	/* Open load and verify BPF application */
	skel = psi_pf_bpf__open_and_load();
	if (!skel) {
		fprintf(stderr, "Failed to open BPF skeleton\n");
		return 1;
	}

	/* Attach tracepoint handler */
	err = psi_pf_bpf__attach(skel);
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

	struct perf_buffer *pb = NULL;
	pb = perf_buffer__new(bpf_map__fd(skel->maps.pb), 8 /* 32KB per CPU */, handle_event, NULL, NULL, NULL);
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
	psi_pf_bpf__destroy(skel);
	return -err;
}

