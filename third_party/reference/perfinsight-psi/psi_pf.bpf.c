// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright (c) 2021 Sartura */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

char LICENSE[] SEC("license") = "Dual BSD/GPL";

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
	u64 total_p;				//总压力
};



#define NSEC_PER_USEC (1000L)

#define FSHIFT		11		/* nr of bits of precision */
#define FIXED_1		(1<<FSHIFT)	/* 1.0 as fixed-point */
#define LOAD_INT(x) ((x) >> FSHIFT)
#define LOAD_FRAC(x) LOAD_INT(((x) & (FIXED_1-1)) * 100)

//total的值显示的不太合适

struct {
  __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
  __uint(key_size, sizeof(int));
  __uint(value_size, sizeof(int));
} pb SEC(".maps");

struct {
  __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
  __uint(max_entries, 512);
  __type(key, int);
  __type(value, struct psidata);
} heap SEC(".maps");





SEC("kprobe/update_averages")
int BPF_KPROBE(update_averages,struct psi_group *group, u64 now)
{
	struct psidata * psi_p;
	int zero = 0;
	psi_p = bpf_map_lookup_elem(&heap, &zero);
	if (!psi_p) /* can't happen */
		return 0;

	pid_t pid;
	pid = bpf_get_current_pid_tgid() >> 32;
	psi_p->pid = pid;

	// 获取当前进程的名称（命令行）
 	if (bpf_get_current_comm(psi_p->comm, sizeof(psi_p->comm)))
    	psi_p->comm[0] = 0; // 如果获取失败，将 comm 字段置为空字符串

	unsigned long avg[6][3];
	unsigned long avg_t[18];
	bpf_core_read(&avg_t,sizeof(avg_t),&group->avg);
	//将 avg_t[18]中的元素按顺序放入avg[6][3]中
	int k = 0;
	for (int i = 0; i < 6; i++) {
		for (int j = 0; j < 3; j++) {
			avg[i][j] = avg_t[k];
			k++;
		}
	}

	u64 total[2][6];
	u64 total_t[12];
	bpf_core_read(&total_t,sizeof(total_t),&group->total);
	//将 total_t[12];中的元素按顺序放入total[2][6]中
	int l = 0;
	for (int i = 0; i < 2; i++) {
		for (int j = 0; j < 6; j++) {
			total[i][j] = total_t[l];
			l++;
		}
	}

	int res; //res==0时为IO，res为1是Memory res为2是CPU
	for (res=0;res<3;res++){
		if(res == 0 ){
			psi_p->sub=0;
			//bpf_probe_read_str(&psi_p->subsystem,sizeof(psi_p->index),"IO");
		}else if(res == 1){
			//bpf_probe_read_str(&psi_p->subsystem,sizeof(psi_p->index),"MEMORY");
			psi_p->sub=1;
		}
		else if(res==2){
			//bpf_probe_read_str(&psi_p->subsystem,sizeof(psi_p->index),"CPU");
			psi_p->sub=2;
		}
		int full;
		for(full = 0;full < 2;full++){
			unsigned long avg_p[3] = { 0, };
			u64 total_p = 0;

			if(full==0){
				//bpf_probe_read_str(&psi_p->index,sizeof(psi_p->index),"SOME");
				psi_p->indexnum=0;
			}else if(full==1){
				//bpf_probe_read_str(&psi_p->index,sizeof(psi_p->index),"FULL");
				psi_p->indexnum=1;
			}
			if(res==2 && full==1){//不存在 CPU FULL 的情况
				full=0;
				res=0;
				return 0;
			}
				int w;
				for (w = 0; w < 3; w++){
					avg_p[w] = avg[ res * 2 + full][w];
					psi_p->avg_p[w]=avg_p[w];
				}
				total_p = total[0][res * 2 + full] / NSEC_PER_USEC;
				psi_p->total_p = total_p;

				// bpf_printk(" total=%llu\n",
				// 	 LOAD_INT(avg_p[0]), LOAD_FRAC(avg_p[0]),
				// 	 LOAD_INT(avg_p[1]), LOAD_FRAC(avg_p[1]),
				// 	 LOAD_INT(avg_p[2]), LOAD_FRAC(avg_p[2]),
				// 	total_p);
				bpf_perf_event_output(ctx, &pb, BPF_F_CURRENT_CPU, psi_p, sizeof(*psi_p));
			
		}
	}

	return 0;
}


