#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <sys/resource.h>
#include "bpf/libbpf.h"	
#include "latency_5.skel.h"

typedef unsigned long long u64;
#define TASK_COMM_LEN	 16
typedef u64 sector_t;
#define MAX_FILENAME_LEN 127


struct mydata_t{			//���յ�������
 	u64 pid;
    u64 num;            //mm
    u64 enter_timestamp;    
    u64 time;  
    u64 run_timestamp;   
    u64 ktimestamp;     //mm    
    u64 jtimestamp;     //mm
    u64 delay;          //mm
    char comm[TASK_COMM_LEN];  
    int cpu;
	char filename[MAX_FILENAME_LEN];
    u64 start_time; //��ʼʱ��      //rw
	u64 end_time;                   //rw
	u64 diff;                       //rw
	u64 write_speed; //���ٶ�       //rw
	u64 read_speed; //д�ٶ�        //rw    
	u64 read_bytes; //��            //rw
	u64 write_bytes; //д           //rw
    unsigned short common_type;             //rt
    unsigned char common_flags;             //rt
    unsigned char common_preempt_count;      //rt
    int common_pid;                         //rt
    u64 runtime;                            //rt
    u64 vruntime;                           //rt
    unsigned int cur;                       //rt
    u64 insert_time;                        //io
    u64 issue_time;                         //io
    u64 complete_time;                      //io
    sector_t sector;                        //io
    unsigned long latency;                  //io
    unsigned long queue_time;               //io
    unsigned long execution_time;           //io
};

struct mydata{
	unsigned int count;
    int cpu;
};

static int libbpf_print_fn(enum libbpf_print_level level, const char *format, va_list args)
{
	return vfprintf(stderr, format, args);
	//ʹ�� vfprintf ����ʽ������Ϣ��ӡ����׼��������stderr����
}

static volatile sig_atomic_t stop;

static void sig_int(int signo)				//���� SIGINT �ź�, �Ա��ڽ��յ��ж��ź�ʱֹͣ�����ִ��
{
	stop = 1;
}

// �õ���ǰʱ���ַ�������ȷ������
// ����һ���ַ��������������������ĳ���
void get_time_str_ms(char *time_str, int len)
{
	struct timeval tv;
	struct timezone tz;
	struct tm *p;
	
	memset(time_str, 0, len);
	
	gettimeofday(&tv,&tz);

	p = localtime(&tv.tv_sec);

	sprintf(time_str,"%d-%02d-%02d %02d:%02d:%02d.%03ld",
		(1900+p->tm_year),(1+p->tm_mon),p->tm_mday,(p->tm_hour),p->tm_min,p->tm_sec,tv.tv_usec/1000);
}


void handle_event(void *ctx, int cpu, void *data, unsigned int data_sz)
{
    const struct mydata_t *md = data;
	char str[30];
    get_time_str_ms(str, sizeof(str));
	if (md->delay > (30000000)){
		printf("%-8llu  %-20llu  %-20llu  %-20lu  %-3s  %-5llu  %-5llu  %-5llu  %-5llu  %-llu  %-llu      %lu  %lu  \n", md->pid, 
						md->time, md->delay, md->latency, md->comm, md->read_bytes, md->read_speed, md->write_bytes, md->write_speed, md->runtime, md->vruntime,
								md->queue_time, md->execution_time);

	}
	}

void handle_event_t(void *ctx, int cpu, void *data, unsigned int data_sz)
{
    const struct mydata *md = data;
	char str[30];
    get_time_str_ms(str, sizeof(str));
	printf("Cpu:[%5u]	Count:%5u\n", md->cpu, md->count);
}

int main(int argc, char **argv)
{
	struct latency_5_bpf *skel;
	int err;

	/* Set up libbpf errors and debug info callback */
	libbpf_set_print(libbpf_print_fn);				//�ڷ����������Ҫ���������Ϣʱ�����ָ���Ļص�����

	/* Open load and verify BPF application */
	skel = latency_5_bpf__open_and_load();				//���غ���֤BPFӦ�ó���
	if (!skel) {
		fprintf(stderr, "Failed to open BPF skeleton\n");
		return 1;
	}
	/* Attach tracepoint handler */
	err = latency_5_bpf__attach(skel);		//�����������Ѽ��غͳ�ʼ����BPF�������
	if (err) {								// ����ʧ��
		fprintf(stderr, "Failed to attach BPF skeleton\n");
		goto cleanup;
	}
	if (signal(SIGINT, sig_int) == SIG_ERR) {
		fprintf(stderr, "can't set signal handler: %s\n", strerror(errno));
		goto cleanup;
	}

	struct perf_buffer *pb = NULL;
	struct perf_buffer *pb_c = NULL;
	pb = perf_buffer__new(bpf_map__fd(skel->maps.perf), 8 /* 32KB per CPU */, handle_event, NULL, NULL, NULL);
	pb_c = perf_buffer__new(bpf_map__fd(skel->maps.perf_c), 8 /* 32KB per CPU */, handle_event_t, NULL, NULL, NULL);
	if(libbpf_get_error(pb)) {		
		err = -1;
		fprintf(stderr, "Failed to create perf buffer\n");
		goto cleanup;
	}
	if(libbpf_get_error(pb_c)) {
		err = -1;
		fprintf(stderr, "Failed to create perf buffer\n");
		goto cleanup;
	}
	// int k = 10;
	printf("pid		latency(cpu)		latency(mm)		latency(io)		comm	count[cpu]	read_bytes		read_speed[kb/s]		write_bytes		write_speed[kb/s]		runtime[ns]		vruntime[ns]		[IOqueue_time]		[IOexecution_time]\n");
    //int wrw = 3;
	while(1){
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

		err = perf_buffer__poll(pb_c, 100 /* timeout, ms */);
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
	latency_5_bpf__destroy(skel);			//����֮ǰ������BPF������� (skel), ȷ���ͷ������Դ
	return -err;				//������, ������ -err ����ʾ����ķ���, �������봫�ݸ����÷�
}