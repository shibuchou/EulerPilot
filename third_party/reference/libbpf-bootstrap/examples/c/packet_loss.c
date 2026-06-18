#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <sys/resource.h>
#include "bpf/libbpf.h"
#include "kmalloc.skel.h"
typedef int64_t u64;
struct my_data {
    int pid;
    int cpu;
    u64 timestamp;
    size_t size;
    int gfp;
    int flags;
    int order;
    int migratetype;
    int pfn;
    int is_kmalloc; 
};


static int libbpf_print_fn(enum libbpf_print_level level, const char *format, va_list args)
{
    if (level == LIBBPF_DEBUG)
        return 0;
    return vfprintf(stderr, format, args);
}

void bump_memlock_rlimit(void)
{
    struct rlimit rlim_new = { .rlim_cur = RLIM_INFINITY, .rlim_max = RLIM_INFINITY };
    if (setrlimit(RLIMIT_MEMLOCK, &rlim_new)) {
        fprintf(stderr, "Failed to relimit OS source");
        exit(1);
    }
}

static volatile bool exiting = false;

static void sig_handler(int sig)
{
    exiting = true;
}

void handle_event(void *ctx, int cpu, void *data, unsigned int data_sz)
{
    const struct my_data *md = data;
	if(md->is_kmalloc==1){
		printf("[memory alloc]\n");
        printf("kmalloc:[%2d] pid:%-5d Timestamp: %-12lu, Size: %zu, GFP Flags: %d \n", md->cpu, md->pid,
                md->timestamp, md->size, md->gfp);
	}else{
		printf("[memory free]\n");
        printf("kfree:[%2d] pid:%-5d Timestamp: %-12lu, Size: %zu \n", md->cpu, md->pid,
                md->timestamp, md->size);
	}
   
}

int main(int argc, char **argv) {
    struct kmalloc_bpf *skel;
    int err ;
    bump_memlock_rlimit();

    libbpf_set_print(libbpf_print_fn);
    
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    
    skel = kmalloc_bpf__open();
    if (!skel) {
		fprintf(stderr, "Failed to open and verify BPF skeleton\n");
		goto cleanup;
	}

    err = kmalloc_bpf__load(skel);
	if (err) {
		fprintf(stderr, "Failed to load BPF skeleton\n");
		goto cleanup;
	}

    err = kmalloc_bpf__attach(skel);
	if (err) {
		fprintf(stderr, "Failed to attach BPF skeleton\n");
		goto cleanup;
	}

    struct perf_buffer *pb = NULL;
    pb = perf_buffer__new(bpf_map__fd(skel->maps.events_map), 8 /* 32KB per CPU */, handle_event, NULL, NULL, NULL);
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
 
    kmalloc_bpf__destroy(skel);
    return err < 0 ? -err : 0;
}