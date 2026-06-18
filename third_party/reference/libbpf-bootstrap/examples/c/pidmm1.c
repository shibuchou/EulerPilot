#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <sys/resource.h>
#include "bpf/libbpf.h"
#include "pidmm1.skel.h"
typedef __int64_t u64;

struct pidmm_t {
    int pid;
    int cpu;
    char prev_comm[16];
	pid_t prev_pid;
	int prev_prio;
	long int prev_state;
	char next_comm[16];
	pid_t next_pid;
	int next_prio;
    int pgd;
    long unsigned int total_vm;
    long unsigned int data;
    long unsigned int text;
    long unsigned int shared;
    long unsigned int resident;
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

/*void get_time_str_ms(char *time_str, int len)
{
  struct timeval tv;
  struct timezone tz;
  struct tm *p;
  
  memset(time_str, 0, len);
  
  gettimeofday(&tv,&tz);

  p = localtime(&tv.tv_sec);

  sprintf(time_str,"%d-%02d-%02d %02d:%02d:%02d.%03ld",
    (1900+p->tm_year),(1+p->tm_mon),p->tm_mday,(p->tm_hour),p->tm_min,p->tm_sec,tv.tv_usec/1000);
}*/

void handle_event(void *ctx, int cpu, void *data, unsigned int data_sz)
{
    const struct pidmm_t *md = data;
    struct tm *tm; 
	char str[32];
 	time_t t;
 	time(&t);
 	tm = localtime(&t);
 	strftime(str, sizeof(str), "%H:%M:%S", tm);
    /*char str[30];
	get_time_str_ms(str, sizeof(str));*/
    printf( "   CPU     PID       TIME          total_vm          data         text        shared        resident\n");
    printf( "   %2d     %-5d    %s            %ld         %ld        %ld          %ld        %ld\n",md->cpu, md->prev_pid, str,md->total_vm,md->data,md->text,md->shared,md->resident); 
      
}

int main(int argc, char **argv) {
    struct pidmm1_bpf *skel;
    int err ;
    bump_memlock_rlimit();

    libbpf_set_print(libbpf_print_fn);
    
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    
    skel = pidmm1_bpf__open();
    if (!skel) {
		fprintf(stderr, "Failed to open and verify BPF skeleton\n");
		goto cleanup;
	}

    err = pidmm1_bpf__load(skel);
	if (err) {
		fprintf(stderr, "Failed to load BPF skeleton\n");
		goto cleanup;
	}

    err = pidmm1_bpf__attach(skel);
	if (err) {
		fprintf(stderr, "Failed to attach BPF skeleton\n");
		goto cleanup;
	}

    struct perf_buffer *pb = NULL;
    pb = perf_buffer__new(bpf_map__fd(skel->maps.mmevents), 8 /* 32KB per CPU */, handle_event, NULL, NULL, NULL);
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
 
    pidmm1_bpf__destroy(skel);
    return err < 0 ? -err : 0;
}