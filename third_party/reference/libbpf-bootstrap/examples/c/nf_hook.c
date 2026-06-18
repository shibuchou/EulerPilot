#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <linux/bpf.h>
#include <sys/resource.h>
#include "bpf/libbpf.h"
#include "nf_hook.skel.h"
typedef int64_t u64;
typedef int32_t u32;

struct data_t {
    u64 start_time;
    u64 cost_time;
    u64 count;
    u32 pakhash;
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

int ringbuf_event_handler(void *ctx,  void *data, size_t data_sz)
{
    const struct data_t *md = data;
    printf("pakhash:[%-5u]  nf_hook_time: %-12ld,count: %ld \n", 
    md->pakhash, md->cost_time, md->count);
    return 0;
}

int main(int argc, char **argv) {
    struct nf_hook_bpf *skel;
    int err ;
    bump_memlock_rlimit();

    libbpf_set_print(libbpf_print_fn);
    
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    
    skel = nf_hook_bpf__open();
    if (!skel) {
		fprintf(stderr, "Failed to open and verify BPF skeleton\n");
		goto cleanup;
	}

    err = nf_hook_bpf__load(skel);
	if (err) {
		fprintf(stderr, "Failed to load BPF skeleton\n");
		goto cleanup;
	}

    err = nf_hook_bpf__attach(skel);
	if (err) {
		fprintf(stderr, "Failed to attach BPF skeleton\n");
		goto cleanup;
	}

    struct ring_buffer *rb = NULL;
    rb = ring_buffer__new(bpf_map__fd(skel->maps.rb), ringbuf_event_handler, NULL, NULL);
	if (!rb) {
		err = -1;
		fprintf(stderr, "Failed to create ring buffer\n");
		goto cleanup;
	}


   while (!exiting) {
		err = ring_buffer__poll(rb, -1);
		if (err == -EINTR) {
			err = 0;
			break;
		}
		if (err < 0) {
			printf("Error polling ring buffer: %d\n", err);
			break;
		}
	}
   
cleanup:
 
    nf_hook_bpf__destroy(skel);
    return err < 0 ? -err : 0;
}