#include "vmlinux.h"
#include "bpf/bpf_helpers.h"
#include "bpf/bpf_tracing.h"
#include "bpf/bpf_core_read.h"

char LICENSE[] SEC("license") = "Dual BSD/GPL";

struct pakage {
    int cpu;
    __u64 skbaddr;
    //__u64 location;
    int location;
    short unsigned int protocol;
    int reason;
};


struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1024);
	__type(key, int);
	__type(value,struct pakage);
}packet_events SEC(".maps");

// struct {
// 	__uint(type, BPF_MAP_TYPE_HASH);
// 	__uint(max_entries, 1024);
// 	__type(key, struct sk_buff *);
// 	__type(value,struct pakage);
// }packet_events SEC(".maps");

struct   {
    __uint(type ,BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size , sizeof(pid_t));
    __uint(value_size , sizeof(int));
}perf_map SEC(".maps");

/*enum skb_drop_reason {
        SKB_NOT_DROPPED_YET = 0,//数据包尚未被丢弃。
        SKB_CONSUMED = 1,//数据包已被处理，不需要释放。
        SKB_DROP_REASON_NOT_SPECIFIED = 2,//未指定具体的丢弃原因。
        SKB_DROP_REASON_NO_SOCKET = 3,//没有找到对应的 socket。
        SKB_DROP_REASON_PKT_TOO_SMALL = 4,//数据包小于 TCP 头的大小。
        SKB_DROP_REASON_TCP_CSUM = 5,//TCP 校验和错误。
        SKB_DROP_REASON_SOCKET_FILTER = 6,//socket 过滤器明确要求丢弃。
        SKB_DROP_REASON_UDP_CSUM = 7,//UDP 校验和错误。
        SKB_DROP_REASON_NETFILTER_DROP = 8,//被 netfilter 丢弃。
        SKB_DROP_REASON_OTHERHOST = 9,//目标主机不是本机。
        SKB_DROP_REASON_IP_CSUM = 10,//IP 校验和错误。
        SKB_DROP_REASON_IP_INHDR = 11,//IP 头错误。
        SKB_DROP_REASON_IP_RPFILTER = 12,//反向路径过滤器（rp_filter）丢弃。
        SKB_DROP_REASON_UNICAST_IN_L2_MULTICAST = 13,//在二层多播中收到单播数据包。
        
};*/



SEC("tracepoint/skb/kfree_skb")
int handle_kfree_skb(struct trace_event_raw_kfree_skb *ctx){   
    struct pakage* data;
    int cpu;
    cpu = bpf_get_smp_processor_id();
    // struct sk_buff *sk = (struct sk_buff *)ctx->skbaddr;
    // __u64 addr = (__u64)sk;
    // struct kfree_skb *skb = (struct kfree_skb *)ctx->location;
    // __u64 address = (__u64)skb;
    __u64 addr = 0;
    int address = 0;
    int reason = 0;
    int protocol = 0;
    data = bpf_map_lookup_elem(&packet_events, &cpu);
	if (!data) {
        struct pakage new_data = {};
        new_data.skbaddr = addr;
        new_data.cpu = cpu;
        new_data.location = address;
        new_data.protocol = protocol;
        // new_data.reason = ctx->reason;
        new_data.reason = reason;
        bpf_map_update_elem(&packet_events, &cpu, &new_data, BPF_ANY);
    }else{
        data->cpu = cpu;
        data->location = address;
        data->protocol = protocol;
        // data->reason = ctx->reason;
        data->reason = reason;
        bpf_map_update_elem(&packet_events, &cpu, data, BPF_ANY);
    }
    bpf_perf_event_output(ctx, &perf_map, BPF_F_CURRENT_CPU, data, sizeof(*data));
    return 0;
}


