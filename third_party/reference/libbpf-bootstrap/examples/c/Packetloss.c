#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <sys/resource.h>
#include "bpf/libbpf.h"
#include "Packetloss.skel.h"
typedef int64_t u64;
typedef int32_t u32;

struct pakage {
    int cpu;
    u64 skbaddr;
    u64 location;
    short unsigned int protocol;
    int reason;
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
    const struct pakage *md = data;
    printf("Packetloss:[%2d] skbaddr:%-5lu location: %-12lu, protocol: %d, reason: %u \n", 
    md->cpu, md->skbaddr, md->location, md->protocol, md->reason);
   
}

int main(int argc, char **argv) {
    struct Packetloss_bpf *skel;
    int err ;
    bump_memlock_rlimit();

    libbpf_set_print(libbpf_print_fn);
    
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    
    skel = Packetloss_bpf__open();
    if (!skel) {
		fprintf(stderr, "Failed to open and verify BPF skeleton\n");
		goto cleanup;
	}

    err = Packetloss_bpf__load(skel);
	if (err) {
		fprintf(stderr, "Failed to load BPF skeleton\n");
		goto cleanup;
	}

    err = Packetloss_bpf__attach(skel);
	if (err) {
		fprintf(stderr, "Failed to attach BPF skeleton\n");
		goto cleanup;
	}

    struct perf_buffer *pb = NULL;
    pb = perf_buffer__new(bpf_map__fd(skel->maps.perf_map), 8 /* 32KB per CPU */, handle_event, NULL, NULL, NULL);
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
 
    Packetloss_bpf__destroy(skel);
    return err < 0 ? -err : 0;
}