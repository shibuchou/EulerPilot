#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

char LICENSE[] SEC("license") = "Dual BSD/GPL";

#define MAX_PIDS 10

// 定义全局变量，用于存储目标进程的PID
int pid_target_0;
int pid_target_1;
int pid_target_2;
int pid_target_3;
int pid_target_4;

struct fileinfo{
	u64 opentimestamp;
	u64 time;
	pid_t pid;
	char appname[128];
	char filename[256];
	u64 fd;
};
struct {
	__uint(type,BPF_MAP_TYPE_PERF_EVENT_ARRAY);
	__type(key,int);
	__type(value,int);
}perf SEC(".maps");

struct {
	__uint(type,BPF_MAP_TYPE_ARRAY);
	__uint(max_entries,8192);
	__type(key,int);
	__type(value,struct fileinfo);
}fileinfo SEC(".maps");


SEC("kretprobe/get_unused_fd_flags")
int BPF_KRETPROBE(get_unused_fd_flags, long ret)
{
	pid_t pid;
	pid = bpf_get_current_pid_tgid() >> 32;


	struct fileinfo mydata = {};
	mydata.fd = ret;
	bpf_map_update_elem(&fileinfo,&pid,&mydata,BPF_ANY);
	return 0;
}


SEC("kprobe/do_filp_open")
int BPF_KPROBE(do_filp_open,int dfd, struct filename *name)
{	
	pid_t pid;
	pid = bpf_get_current_pid_tgid() >> 32;
	
	if (pid_target_0 != 0 && (pid != pid_target_0 && pid != pid_target_1 && pid != pid_target_2 && pid != pid_target_3 && pid != pid_target_4)) {
		return 0;
	}
	// //数组过滤
	// if (pid_target[0] != 0) {
	// 	int i = 0;
	// 	while (pid_target[i] != 0) {
	// 		if (pid_target[i] == pid) {
	// 			break;  // 当前进程的ID与目标进程ID匹配，退出循环
	// 		}
	// 		i++;
	// 	}

	// 	if (pid_target[i] == 0) {
	// 		return 0;  // 当循环结束时仍未找到匹配的目标进程ID，返回0
	// 	}
	// }

	struct fileinfo *mydata = bpf_map_lookup_elem(&fileinfo,&pid);
	if (mydata){
		mydata->pid = pid;
		const char *filename;
		bpf_ktime_get_ns();
		filename = BPF_CORE_READ(name, name);
		bpf_probe_read_str(mydata->filename, sizeof(mydata->filename), filename);
		bpf_get_current_comm(&mydata->appname, sizeof(mydata->appname));
		u64 timestamp = bpf_ktime_get_ns();
		mydata->opentimestamp = timestamp;

		bpf_perf_event_output(ctx, &perf, BPF_F_CURRENT_CPU, mydata, sizeof(*mydata));
		bpf_map_delete_elem(&fileinfo, &pid);  
	}
	return 0;
}

