#include "vmlinux.h"
#include "bpf/bpf_helpers.h"
#include "bpf/bpf_tracing.h"
#include "bpf/bpf_core_read.h"

char LICENSE[] SEC("license") = "Dual BSD/GPL";

struct net_t {
    int cpu;
    __u64 skbaddr;
    // unsigned int mark;
    u64 start_time;
    u64 finish_time;
    u64 transmission_time;
    unsigned int len;
    __u32 loc_name;
};



/*struct trace_event_raw_net_dev_xmit {
	struct trace_entry ent;
	void *skbaddr;
	unsigned int len;
	int rc;
	u32 __data_loc_name;
	char __data[0];
};*/

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1024);
	__type(key, struct sk_buff *);
	__type(value,struct net_t);
}net_events SEC(".maps");

struct   {
    __uint(type ,BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size , sizeof(pid_t));
    __uint(value_size , sizeof(int));
}perf_map SEC(".maps");

SEC("tp/net/net_dev_start_xmit")
int handle_net_dev_start_xmit(struct trace_event_raw_net_dev_start_xmit *ctx){   
    struct net_t* data;
    u64 starttm;
    starttm = bpf_ktime_get_ns();
    int cpu;
    cpu = bpf_get_smp_processor_id();
    struct sk_buff *sk = (struct sk_buff *)ctx->skbaddr;
    __u64 addr = (__u64)sk;
    data = bpf_map_lookup_elem(&net_events, &sk);
	if (!data) {
        struct net_t new_data = {};
        new_data.skbaddr = addr;
        new_data.cpu = cpu;
        new_data.start_time = starttm;
        new_data.len = ctx->len;
        // bpf_probe_read(&new_data.len, sizeof(ctx->len),&ctx->len);
        new_data.finish_time = 0;
        new_data.transmission_time = 0;
        new_data.loc_name = ctx->__data_loc_name;
        bpf_map_update_elem(&net_events, &sk, &new_data, BPF_ANY);
    }else{
        data->start_time = starttm;
        bpf_map_update_elem(&net_events, &sk, data, BPF_ANY);
    }

    return 0;
}

SEC("tp/net/net_dev_xmit")
int handle_net_dev_xmit(struct trace_event_raw_net_dev_xmit *ctx){   
    struct net_t* data;
    u64 finish;
    finish = bpf_ktime_get_ns();
    struct sk_buff *sk = (struct sk_buff *)ctx->skbaddr;
    __u64 addr = (__u64)sk;
    data = bpf_map_lookup_elem(&net_events, &sk);
	if (!data) 
        return 0;

    data->finish_time = finish;
    data->transmission_time = finish - data->start_time;
    // bpf_probe_read(&data->comm, sizeof(ctx->comm),&ctx->comm);
    bpf_map_delete_elem(&net_events, &sk);
    bpf_perf_event_output(ctx, &perf_map, BPF_F_CURRENT_CPU, data, sizeof(*data));
    return 0;
}
