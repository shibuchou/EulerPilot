#include "vmlinux.h"

#include <bpf/bpf_endian.h>
#include <bpf/bpf_helpers.h>

#define ETH_P_IP 0x0800
#define IPPROTO_ICMP 1
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17
#define TC_ACT_OK 0

char LICENSE[] SEC("license") = "GPL";

struct network_qos_config {
    __u16 dst_port;
    __u8 protocol;
    __u8 enabled;
    __u32 reserved;
};

struct network_qos_stats {
    __u64 packet_count;
    __u64 byte_count;
};

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct network_qos_config);
} qos_config_map SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct network_qos_stats);
} qos_stats_map SEC(".maps");

static __always_inline bool match_l4_port(void *data_end, struct iphdr *iph,
                                          __u16 dst_port)
{
    void *l4 = (void *)iph + (iph->ihl * 4);

    if (dst_port == 0) {
        return true;
    }
    if (iph->protocol == IPPROTO_TCP) {
        struct tcphdr *tcp = l4;
        if ((void *)(tcp + 1) > data_end) {
            return false;
        }
        return bpf_ntohs(tcp->dest) == dst_port;
    }
    if (iph->protocol == IPPROTO_UDP) {
        struct udphdr *udp = l4;
        if ((void *)(udp + 1) > data_end) {
            return false;
        }
        return bpf_ntohs(udp->dest) == dst_port;
    }
    return false;
}

SEC("classifier")
int network_qos_classifier(struct __sk_buff *skb)
{
    __u32 key = 0;
    void *data = (void *)(long)skb->data;
    void *data_end = (void *)(long)skb->data_end;
    struct network_qos_config *config;
    struct network_qos_stats *stats;
    struct ethhdr *eth = data;
    struct iphdr *iph;

    if ((void *)(eth + 1) > data_end) {
        return TC_ACT_OK;
    }
    if (bpf_ntohs(eth->h_proto) != ETH_P_IP) {
        return TC_ACT_OK;
    }

    iph = (void *)(eth + 1);
    if ((void *)(iph + 1) > data_end || iph->ihl < 5) {
        return TC_ACT_OK;
    }

    config = bpf_map_lookup_elem(&qos_config_map, &key);
    if (!config || !config->enabled) {
        return TC_ACT_OK;
    }
    if (config->protocol != 0 && config->protocol != iph->protocol) {
        return TC_ACT_OK;
    }
    if ((iph->protocol == IPPROTO_TCP || iph->protocol == IPPROTO_UDP) &&
        !match_l4_port(data_end, iph, config->dst_port)) {
        return TC_ACT_OK;
    }
    if (config->protocol == IPPROTO_ICMP && iph->protocol != IPPROTO_ICMP) {
        return TC_ACT_OK;
    }

    stats = bpf_map_lookup_elem(&qos_stats_map, &key);
    if (stats) {
        __sync_fetch_and_add(&stats->packet_count, 1);
        __sync_fetch_and_add(&stats->byte_count, skb->len);
    }
    return TC_ACT_OK;
}
