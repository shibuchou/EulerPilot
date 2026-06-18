#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf/bpf_core_read.h"

#define TASK_COMM_LEN	 32
#define TASK_RUNNING 0
#define MAX_FILENAME_LEN 127


char LICENSE[] SEC("license") = "Dual BSD/GPL";

struct sched_switch_args {
    struct task_struct *prev;
    struct task_struct *next;
};

struct {                                            //main perf
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __type(key, int);
    __type(value, int);
} perf SEC(".maps");

struct {                                            //len perf
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __type(key, int);
    __type(value, int);
} perf_c SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 8192);
    __type(key, int);
    __type(value, struct pid_data);
} pid_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct cpu_data);
} cpu_map SEC(".maps");

//=========================io=============================//
struct test {
	unsigned short common_type;
	unsigned char common_flags;
	unsigned char common_preempt_count;
	int common_pid;
	dev_t dev;
	sector_t sector;
	unsigned int nr_sector;
	int error;
	char rwbs[8];
};

struct meta{
    char comm[TASK_COMM_LEN];
    pid_t pid;
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 8192);
	__type(key, sector_t);
	__type(value, u64);
} blk_insert SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 8192);
	__type(key, sector_t);
	__type(value, u64);
} blk_issue SEC(".maps"); 

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1024);
	__type(key, sector_t);
	__type(value, struct meta);
} metaa SEC(".maps");

struct {                                                        //map
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct pid_data);
} heap SEC(".maps");

//==========================io============================//

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 8192);
    __type(key, pid_t);
    __type(value, struct pid_data);
} my_data_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1024);
	__type(key, pid_t);
	__type(value, struct pid_data);
} data_map SEC(".maps");


//   -----------------���������ݵĽṹ��-------------------   //
struct pid_data {
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

struct cpu_data{
    unsigned int count;
    int cpu;
};

SEC("kprobe/enqueue_task_fair")
int kprobe_enqueue_task_fair(struct pt_regs *ctx) 
{
    struct pid_data my_data = {};
    u64 timestamp = bpf_ktime_get_ns();
    u64 pid = bpf_get_current_pid_tgid() >> 32;
    my_data.enter_timestamp = bpf_ktime_get_ns();
    bpf_map_update_elem(&pid_map, &pid, &my_data, BPF_ANY);
    return 0;
}

SEC("kprobe/dequeue_task_fair")
int kprobe_dequeue_task_fair(struct pt_regs *ctx) 
{
    u64 pid = bpf_get_current_pid_tgid() >> 32;
    u64 ts = bpf_ktime_get_ns();
    struct pid_data *mydata = bpf_map_lookup_elem(&pid_map, &pid);
    if(mydata){
        if(mydata->enter_timestamp == 0)  return 0;
        mydata->pid = pid;
        bpf_get_current_comm(&mydata->comm, sizeof(mydata->comm));
        mydata->run_timestamp = ts;
        mydata->time = mydata->run_timestamp - mydata->enter_timestamp;
        bpf_perf_event_output(ctx, &perf, BPF_F_CURRENT_CPU, mydata, sizeof(*mydata));
        bpf_map_delete_elem(&pid_map, &pid);  
    }
    else{
        return 0;
    }
    return 0;
}

//--------------------handlemmfault----------------------//

SEC("kprobe/handle_mm_fault")
int BPF_KPROBE(handle_mm_fault)
{
    pid_t pid;
    struct pid_data data ={};
    pid = bpf_get_current_pid_tgid() >> 32;
    u64 ktimestamp = bpf_ktime_get_ns();
    data.pid = pid;
    data.ktimestamp = ktimestamp;
    data.num++; //��ȡҳ������Ĵ���
    bpf_map_update_elem(&my_data_map, &pid, &data, BPF_ANY);

    return 0;
}

SEC("kretprobe/handle_mm_fault")
int BPF_KRETPROBE(handle_mm_fault_exit)
{
    pid_t pid = bpf_get_current_pid_tgid() >> 32;
    struct pid_data *data = bpf_map_lookup_elem(&my_data_map, &pid);
    
    if (data) {
        bpf_get_current_comm(&data->comm, sizeof(data->comm));
        u64 jtimestamp = bpf_ktime_get_ns();
        data->jtimestamp = jtimestamp;
        data->delay = data->jtimestamp - data->ktimestamp;
        bpf_perf_event_output(ctx, &perf, BPF_F_CURRENT_CPU, data, sizeof(*data));
    }
    return 0;
}

//-----------------io---------------------//

SEC("tp/block/block_rq_insert")
int handle_blk_insert(struct trace_event_raw_block_rq *insert_ctx){   
    struct pid_data* d;
    struct meta m;
    int zero = 0;
    int cpu;
    cpu = bpf_get_smp_processor_id();
    d = bpf_map_lookup_elem(&heap, &zero);
	if (!d) /* can't happen */
		return 0;
    pid_t pid;
    pid = bpf_get_current_pid_tgid() >> 32;
    u64 start_blk_insert;
    start_blk_insert = bpf_ktime_get_ns();
    bpf_get_current_comm(&d->comm, sizeof(d->comm)); 
    d->pid = pid;
    d->cpu = cpu;
    d->insert_time = start_blk_insert;
    d->sector = insert_ctx->sector;
    m.pid = pid;
    struct task_struct *task = (struct task_struct *)bpf_get_current_task();
    bpf_probe_read_str(&m.comm, sizeof(m.comm), task->comm);
    bpf_map_update_elem(&blk_insert, &d->sector, &start_blk_insert, BPF_ANY);
    bpf_map_update_elem(&metaa, &d->sector, &m, BPF_ANY);
    bpf_perf_event_output(insert_ctx, &perf, BPF_F_CURRENT_CPU, d, sizeof(*d));
    return 0;
}

SEC("tp/block/block_rq_issue")
int handle_blk_issue(struct trace_event_raw_block_rq *issue_ctx){   
    struct pid_data* d;
    sector_t sector;
    sector = issue_ctx->sector;
    int zero = 0;
    d = bpf_map_lookup_elem(&heap, &zero);
	if (!d) /* can't happen */
		return 0;
    bpf_map_delete_elem(&heap, &zero);
    u64 *insert_time;
    insert_time = bpf_map_lookup_elem(&blk_insert, &sector);    
    if(insert_time==NULL){
        bpf_printk("cannot find insert_time");
        return 0;
    }
    struct meta* m1;
    m1 = bpf_map_lookup_elem(&metaa, &sector);
    if (!m1){
        bpf_printk("Opps!");
        return 0;
    }
    pid_t pid;
    pid = bpf_get_current_pid_tgid() >> 32;
    u64 start_blk_issue;
    start_blk_issue = bpf_ktime_get_ns();
    d->issue_time = start_blk_issue;
    u64 queue_time;
    queue_time = d->issue_time - *insert_time;
    bpf_get_current_comm(&d->comm, sizeof(d->comm));
    d->pid = m1->pid;
    d->insert_time = *insert_time;
    d->queue_time = queue_time;
    d->sector = sector;
    d->issue_time = bpf_ktime_get_ns();
    bpf_map_update_elem(&blk_issue, &d->sector, &start_blk_issue, BPF_ANY);
    bpf_map_update_elem(&metaa, &d->sector, m1, BPF_ANY);
    bpf_perf_event_output(issue_ctx, &perf, BPF_F_CURRENT_CPU, d, sizeof(*d));
    return 0;
}

SEC("tp/block/block_rq_complete")
int handle_blk_complete(struct test *complete_ctx){
    struct pid_data* d;
    pid_t pid;
    sector_t sector;
    sector = complete_ctx->sector;
    int zero = 0;
    d = bpf_map_lookup_elem(&heap, &zero);
	if (!d) /* can't happen */
		return 0;
    bpf_map_delete_elem(&heap, &zero);
    u64 *insert_time;
    insert_time = bpf_map_lookup_elem(&blk_insert, &sector);    
    if(insert_time==NULL){
        bpf_printk("cannot find insert_time");
        return 0;
    }
    bpf_map_delete_elem(&blk_insert, &sector);
    u64 *issue_time;
    issue_time = bpf_map_lookup_elem(&blk_issue, &sector);    
    if(issue_time==NULL){
        bpf_printk("cannot find issue_time");
        return 0;
    }
    bpf_map_delete_elem(&blk_issue, &sector);
    struct meta *m2;
    m2 = bpf_map_lookup_elem(&metaa, &sector);
    bpf_map_delete_elem(&metaa, &sector);
    if (!m2){
        bpf_printk("Opps!");
        return 0;
    }
    u64 finish_blk_complete;
    finish_blk_complete = bpf_ktime_get_ns();
    d->complete_time = finish_blk_complete;
    u64 execution_time;
    execution_time = d->complete_time - *issue_time;
    u64 latency;
    latency = d->complete_time - *insert_time;
    bpf_get_current_comm(&d->comm, sizeof(d->comm));
    d->pid = m2->pid;
    d->insert_time = *insert_time;
    d->issue_time = *issue_time;
    d->latency = latency;
    d->execution_time = execution_time;
    d->sector = sector;
    d->complete_time = bpf_ktime_get_ns();
    bpf_perf_event_output(complete_ctx, &perf, BPF_F_CURRENT_CPU, d, sizeof(*d));
    return 0;
}


//======================len=====================//

SEC("kprobe/update_rq_clock")
int BPF_KPROBE(update_rq_clock,struct rq *rq){
    struct cpu_data *mydata;
    int key = 0;
    int rqKey = 0;
    int cpu;
    cpu = bpf_get_smp_processor_id();
    mydata = bpf_map_lookup_elem(&cpu_map, &rqKey);
if (!mydata) {
    return 0;
}
else{
    bpf_probe_read_str(&mydata->count,sizeof(rq->nr_running),&rq->nr_running);
    mydata->cpu = cpu;
    bpf_perf_event_output(ctx, &perf_c, BPF_F_CURRENT_CPU, mydata, sizeof(*mydata));
    bpf_map_delete_elem(&cpu_map, &cpu);
    }
return 0;
}

//=================rw================//

//��ؿ�I/O���󣨶Կ��豸����Ӳ�̡�SSD�ȣ��Ķ�д���󣩺�ʱ���ύ����io�����ύʱ����
SEC("tp/block/block_rq_issue")
int handle_rq_issue(struct trace_event_raw_block_rq *ctx)
{
	char comm[TASK_COMM_LEN];
	bpf_get_current_comm(comm, sizeof(comm));

	struct pid_data data = {};
	pid_t pid = bpf_get_current_pid_tgid() >> 32;
	u64 start_t = bpf_ktime_get_ns(); //��ȡ��ʼʱ��

	data.pid = pid;
	data.start_time = start_t;
	bpf_probe_read(data.comm, sizeof(data.comm), comm);

	char rwbs[8];
	bpf_probe_read(rwbs, sizeof(rwbs), ctx->rwbs);

	char operation = rwbs[0];
	u64 my_b = ctx->bytes; //��ȡ�� I/O ������ֽ���

	if (operation == 'W') {
		data.write_bytes = my_b; // д����
	} else if (operation == 'R') {
		data.read_bytes = my_b; // ������
	}

	bpf_map_update_elem(&data_map, &pid, &data, BPF_ANY);
	return 0;
}

//��block io�������ʱ����
SEC("tp/block/block_rq_complete")
int handle_rq_complete(struct trace_event_raw_block_rq *ctx)
{
	char comm[TASK_COMM_LEN];
	bpf_get_current_comm(comm, sizeof(comm));

	struct pid_data *d;
	pid_t pid = bpf_get_current_pid_tgid() >> 32;
	d = bpf_map_lookup_elem(&data_map, &pid);
	if (!d)
		return 0;

	//����ʱ��
	u64 end_time = bpf_ktime_get_ns(); //����ʱ��
	d->end_time = end_time;
	u64 diff = d->end_time - d->start_time;
	d->diff = diff;
	bpf_probe_read(d->comm, sizeof(d->comm), comm);

	char rwbs[8];
	bpf_probe_read(rwbs, sizeof(rwbs), ctx->rwbs);

	char operation = rwbs[0];
	u64 my_b = ctx->bytes;
	if (operation == 'W') {
		d->write_bytes += my_b; // д����
		u64 tmp;
		tmp = d->write_bytes / (1024);
		d->write_speed = tmp * 1000000000 / diff;
	} else if (operation == 'R') {
		d->read_bytes += my_b; // ������
		u64 tmp1;
		tmp1 = d->read_bytes / (1024);
		d->read_speed = tmp1 * 1000000000 / diff;
	}

	bpf_perf_event_output(ctx, &perf, BPF_F_CURRENT_CPU, d, sizeof(*d));
	bpf_map_delete_elem(&data_map, &pid);
	return 0;
}

//=======================runtime===========================//

SEC("tp/sched/sched_stat_runtime")
int handle_sched_stat_runtime(struct trace_event_raw_sched_stat_runtime *ctx,struct cpufreq_policy *policy){   
    struct pid_data* d;
    int cpu;
    cpu = bpf_get_smp_processor_id();
    d = bpf_map_lookup_elem(&heap, &cpu);
	if (d) {
    d->cpu = cpu;
    bpf_probe_read(&d->comm, sizeof(ctx->comm),&ctx->comm);
    d->pid = ctx->pid;
    d->runtime = ctx->runtime;
    d->vruntime = ctx->vruntime;
    }else{
        return 0;
    }
    bpf_map_update_elem(&heap, &d->cpu, d, BPF_ANY);
    bpf_perf_event_output(ctx, &perf, BPF_F_CURRENT_CPU, d, sizeof(*d));
    return 0;
}
SEC("kprobe/cpufreq_set_policy")
int BPF_KPROBE(cpufreq_set_policy, struct cpufreq_policy *policy,struct cpufreq_policy *new_policy){
    //struct cpufreq_policy* pol = policy;
    struct pid_data* d;
    int cpu;
    cpu = bpf_get_smp_processor_id();
    d = bpf_map_lookup_elem(&heap, &cpu);
	if (d) {
    bpf_core_read(&d->cur, sizeof(new_policy->cur), &new_policy->cur);
    /*d->cur = pol->cur;*/
    }else{
        return 0;
    }
    bpf_map_update_elem(&heap, &d->cpu, d, BPF_ANY);
    bpf_perf_event_output(ctx, &perf, BPF_F_CURRENT_CPU, d, sizeof(*d));
    return 0;

    }