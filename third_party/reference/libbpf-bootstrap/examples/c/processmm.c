#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/resource.h>
#include <unistd.h>
#include "bpf/libbpf.h"
#include <pthread.h>
#include "processmm.skel.h"

#define MAX_DATA_COUNT 16

typedef __int64_t u64;

struct pidmm_t {
    __u32 tgid;
    __u32 tid;
    __u32 pid;
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
    long unsigned int test;
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

struct top_data {
    struct pidmm_t data[MAX_DATA_COUNT];
    int count;
};

static struct top_data top_data_list = { .count = 0 };
static struct top_data top_data_list1 = { .count = 0 };

static int compare_resident(const void *a, const void *b) {
    return ((struct pidmm_t *)b)->resident - ((struct pidmm_t *)a)->resident;
}

void format_beijing_time(char *time_str, int len)
{
    time_t rawtime;
    struct tm *timeinfo;

    time(&rawtime);
    timeinfo = gmtime(&rawtime);
    timeinfo->tm_hour += 8;

    strftime(time_str, len, "%Y-%m-%d %H:%M:%S", timeinfo);
}

void clear_top_data_list() {
    top_data_list.count = 0;
}

void clear_top_data_list1() {
    top_data_list1.count = 0;
}

void handle_event(void *ctx, int cpu, void *data, unsigned int data_sz)
{
    const struct pidmm_t *md = data;
    int i=0,flag=0;
    if(md->pid!=0){
        for(i=0;i<top_data_list.count;i++)
        {
            if(md->pid==top_data_list.data[i].pid){
            top_data_list.data[i].resident=(top_data_list.data[i].resident+md->resident)/2;
            flag=1;
        }
        }
        if(flag==0){
            if (top_data_list.count < MAX_DATA_COUNT) {
            top_data_list.data[top_data_list.count++] = *md;
            } else {
            qsort(top_data_list.data, MAX_DATA_COUNT, sizeof(struct pidmm_t), compare_resident);
            if (md->resident > top_data_list.data[MAX_DATA_COUNT - 1].resident) {
                top_data_list.data[MAX_DATA_COUNT - 1] = *md;
            }
            qsort(top_data_list.data, MAX_DATA_COUNT, sizeof(struct pidmm_t), compare_resident);
        }
        }
        }

    int i1=0,flag1=0;
    if(md->tgid!=0){
        for(i1=0;i1<top_data_list1.count;i1++)
        {
            if(md->tgid==top_data_list1.data[i1].tgid){
            top_data_list1.data[i1].resident=(top_data_list1.data[i1].resident+md->resident)/2;
            flag1=1;
        }
        }
        if(flag1==0){
            if (top_data_list1.count < MAX_DATA_COUNT) {
            top_data_list1.data[top_data_list1.count++] = *md;
            } else {
            qsort(top_data_list1.data, MAX_DATA_COUNT, sizeof(struct pidmm_t), compare_resident);
            if (md->resident > top_data_list1.data[MAX_DATA_COUNT - 1].resident) {
                top_data_list1.data[MAX_DATA_COUNT - 1] = *md;
            }
            qsort(top_data_list1.data, MAX_DATA_COUNT, sizeof(struct pidmm_t), compare_resident);
        }
        }
        }
    
}
// 新增一个结构体用于传递数据给新线程
struct ThreadData {
    struct processmm_bpf *skel;
    struct perf_buffer *pb;
};

// 新线程的函数，用于执行数据获取
void *data_collection_thread(void *arg) {
    struct ThreadData *thread_data = (struct ThreadData *)arg;
    struct perf_buffer *pb = thread_data->pb;

    while (!exiting) {
        int err = perf_buffer__poll(pb, 100 /* timeout, ms */);
        if (err == -EINTR) {
            err = 0;
            break;
        }
        if (err < 0) {
            printf("Error polling perf buffer: %d\n", err);
            break;
        }

        // 更新数据等操作

        // time_t now = time(NULL);
        //printf("Data collected at: %s", ctime(&now));

        usleep(1000);  // 小睡5毫秒
    }

    return NULL;
}


int main(int argc, char **argv) {
    struct processmm_bpf *skel;
    int err;
    bump_memlock_rlimit();

    libbpf_set_print(libbpf_print_fn);

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    skel = processmm_bpf__open();
    if (!skel) {
        fprintf(stderr, "Failed to open and verify BPF skeleton\n");
        goto cleanup;
    }

    err = processmm_bpf__load(skel);
    if (err) {
        fprintf(stderr, "Failed to load BPF skeleton\n");
        goto cleanup;
    }

    err = processmm_bpf__attach(skel);
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

    // 创建新线程的数据结构
    struct ThreadData thread_data = {
        .skel = skel,
        .pb = pb
    };

    // 创建新线程
    pthread_t thread;
    if (pthread_create(&thread, NULL, data_collection_thread, (void *)&thread_data) != 0) {
        fprintf(stderr, "Failed to create data collection thread\n");
        goto cleanup;
    }

    while (!exiting) {
        err = perf_buffer__poll(pb, 100 /* timeout, ms */);
        if (err == -EINTR) {
            err = 0;
            break;
        }
        if (err < 0) {
            printf("Error polling perf buffer: %d\n", err);
            break;
        }

 
        // 清屏
        system("clear");

        // 获取当前时间
        char time_str[20];
        format_beijing_time(time_str, sizeof(time_str));

        // 输出前16个最大的数据
        printf("   CPU    PID  TGID           TIME                       pgd           total_vm          data           test          shared       resident\n");
        for (int i = 0; i < top_data_list.count; ++i) {
            const struct pidmm_t *md = &top_data_list.data[i];
            printf("   %2d     %-5u   %-5u   %s       %12u       %8lu         %7lu        %6lu          %6lu        %6lu\n",
                   md->cpu, md->pid,md->tgid, time_str, md->pgd, md->total_vm, md->data, md->test, md->shared, md->resident);
        }

        // 输出前16个最大的数据
        printf("   CPU    TGID                TIME                       pgd           total_vm          data           test          shared       resident\n");
        for (int i1 = 0; i1 < top_data_list1.count; ++i1) {
            const struct pidmm_t *md = &top_data_list1.data[i1];
            printf("   %2d     %-5u         %s       %12u       %8lu         %7lu        %6lu          %6lu        %6lu\n",
                   md->cpu, md->tgid,time_str, md->pgd, md->total_vm, md->data, md->test, md->shared, md->resident);
        }
        
        // 清除数据
        clear_top_data_list();

         // 清除数据
        clear_top_data_list1();

        // 等待五秒
        sleep(5);
    }

    // 等待新线程结束
    pthread_join(thread, NULL);

cleanup:

    processmm_bpf__destroy(skel);
    return err < 0 ? -err : 0;
}


