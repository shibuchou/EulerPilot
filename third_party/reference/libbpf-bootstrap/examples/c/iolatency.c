#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <sys/resource.h>
#include <bpf/libbpf.h>
#include "iolatency.skel.h"
#define TASK_COMM_LEN	 16
#define MAX_FILENAME_LEN 127
typedef int64_t u64;
typedef u64 sector_t;

struct mydata_t {
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
	char filename[MAX_FILENAME_LEN];
};

void bump_memlock_rlimit(void)
{
	struct rlimit rlim_new = { .rlim_cur = RLIM_INFINITY, .rlim_max = RLIM_INFINITY};
	//����+������
	if (setrlimit(RLIMIT_MEMLOCK, &rlim_new)) {
		fprintf(stderr, "Failed to relimit OS source");
		exit(1);
	}
}

static int libbpf_print_fn(enum libbpf_print_level level, const char *format, va_list args)
{
	if (level == LIBBPF_DEBUG)
		return 0;
	return vfprintf(stderr, format, args);
}

static volatile bool exiting = false;

static void sig_handler(int sig)
{
	exiting = true;
}

void handle_event(void *ctx, int cpu, void *data, unsigned int data_sz)
{
    const struct mydata_t *md = data;
	if(md->latency==0){
		printf("------------------IO request----------------\n");
	}else{
		printf("~~~~~~~~~~~~~~~~~~IO Complete~~~~~~~~~~~~~~~\n");
	}
    printf("[IO on] [%2d] pid->%-5d comm->%-3s sector->%-8ld IOinsert_time->%-15lu IOsubmit_time->%-15lu IOcomplete_time->%-12lu [IOqueue_time]->%lu[IOexecution_time]->%lu [iolatency]->%lu ns\n", md->cpu, md->pid, md->comm, md->sector,md->insert_time , md->issue_time, md->complete_time,md->queue_time, md->execution_time, md->latency);


}


int main(int argc, char **argv)
{
	struct iolatency_bpf *skel;
	int err;
    bump_memlock_rlimit();

	/* Set up libbpf errors and debug info callback */
	libbpf_set_print(libbpf_print_fn);

	/* Cleaner handling of Ctrl-C */
	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	/* Load and verify BPF application */
	skel = iolatency_bpf__open();
	if (!skel) {
		fprintf(stderr, "Failed to open and load BPF skeleton\n");
		return 1;
	}

	/* Load & verify BPF programs */
	err = iolatency_bpf__load(skel);
	if (err) {
		fprintf(stderr, "Failed to load and verify BPF skeleton\n");
		goto cleanup;
	}

	/* Attach tracepoints */
	err = iolatency_bpf__attach(skel);
	if (err) {
		fprintf(stderr, "Failed to attach BPF skeleton\n");
		goto cleanup;
	}

    //---------------main logic---------------------------//

    struct perf_buffer *pb = NULL;
	pb = perf_buffer__new(bpf_map__fd(skel->maps.mydata), 8 /* 32KB per CPU */, handle_event, NULL, NULL, NULL);
	if (libbpf_get_error(pb)) {
		err = -1;
		fprintf(stderr, "Failed to create perf buffer\n");
		goto cleanup;
	}

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
	}



cleanup:
	/* Clean up */

	iolatency_bpf__destroy(skel);

	return err < 0 ? -err : 0;
}