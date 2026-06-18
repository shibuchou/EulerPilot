#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <sys/resource.h>
#include "bpf/libbpf.h"
#include "buddystat.skel.h"
typedef __int64_t u64;
#define MAX_ORDER 11

struct mydata_t {
    int pid;
    int cpu;
    u64 alloc_time;
    u64 free_time;
    unsigned int order;
    unsigned long num0;
    unsigned long num1;
    unsigned long num2;
    unsigned long num3;
    unsigned long num4;
    unsigned long num5;
    unsigned long num6;
    unsigned long num7;
    unsigned long num8;
    unsigned long num9;
    unsigned long num10;
    int migratetype;
	unsigned long count;
    unsigned int alloc_flags;
    struct zone *zone;
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
    const struct mydata_t *md = data;  
	printf("[Buddy Info on cpu%2d:      Order:%u]\n",md->cpu,md->order);
    printf("   Order:     0    1    2    3    4    5    6    7    8    9   10  \n");
    printf("Idle number:  %lu    %lu    %lu    %lu    %lu    %lu    %lu    %lu    %lu    %lu    %lu \n",md->num0,md->num1,md->num2,md->num3,md->num4,md->num5,md->num6,md->num7,md->num8,md->num9,md->num10);
    printf(" pid:%-5d Timestamp: %-12lu \n migratetype:",  md->pid, md->alloc_time );
    if(md->migratetype==0){
       printf("UNMOVABLE\n");
       return ;
    }
    if(md->migratetype==1){
       printf("MOVABLE\n");
       return ;
     }
    if(md->migratetype==2){
       printf("RECLAIMABLE\n");
       return ;
    }
    if(md->migratetype==3){
       printf("HIGHATOMIC\n");
       return ;
    }
    if(md->migratetype==4){
       printf("RESERVE\n");
       return ;
    }
    printf("error\n");
    /*switch(md->migratetype){
        case 0:printf("UNMOVABLE\n");break;
        case 1:printf("MOVABLE\n");break;
        case 2:printf("RECLAIMABLE\n");break;
        case 3:printf("HIGHATOMIC\n");break;
        case 4:printf("RESERVE\n");break;
        default:printf("error\n");
    }*/

}

int main(int argc, char **argv) {
    struct buddystat_bpf *skel;
    int err ;
    bump_memlock_rlimit();

    libbpf_set_print(libbpf_print_fn);
    
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    
    skel = buddystat_bpf__open();
    if (!skel) {
		fprintf(stderr, "Failed to open and verify BPF skeleton\n");
		goto cleanup;
	}

    err = buddystat_bpf__load(skel);
	if (err) {
		fprintf(stderr, "Failed to load BPF skeleton\n");
		goto cleanup;
	}

    err = buddystat_bpf__attach(skel);
	if (err) {
		fprintf(stderr, "Failed to attach BPF skeleton\n");
		goto cleanup;
	}

    struct perf_buffer *pb = NULL;
    pb = perf_buffer__new(bpf_map__fd(skel->maps.myevents), 8 /* 32KB per CPU */, handle_event, NULL, NULL, NULL);
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
 
    buddystat_bpf__destroy(skel);
    return err < 0 ? -err : 0;
}