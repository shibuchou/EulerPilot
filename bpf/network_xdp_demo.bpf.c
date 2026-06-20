#include "vmlinux.h"

#include <bpf/bpf_endian.h>
#include <bpf/bpf_helpers.h>

#define ETH_P_IP 0x0800
#define IPPROTO_ICMP 1
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17
#define XDP_POLICY_PASS 0
#define XDP_POLICY_DROP 1

char LICENSE[] SEC("license") = "GPL";

struct network_xdp_config {
    __u16 dst_port;
    __u8 protocol;
    __u8 action;
    __u32 reserved;
};

struct network_xdp_stats {
    __u64 pass_count;
    __u64 drop_count;
    __u64 byte_count;
};

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct network_xdp_config);
} xdp_config_map SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct network_xdp_stats);
} xdp_stats_map SEC(".maps");

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

SEC("xdp")
int network_xdp_filter(struct xdp_md *ctx)
{
    __u32 key = 0;
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;
    struct ethhdr *eth = data;
    struct iphdr *iph;
    struct network_xdp_config *config;
    struct network_xdp_stats *stats;
    __u64 byte_len = (__u64)(ctx->data_end - ctx->data);
    bool matched = false;

    if ((void *)(eth + 1) > data_end) {
        return XDP_PASS;
    }
    if (bpf_ntohs(eth->h_proto) != ETH_P_IP) {
        return XDP_PASS;
    }

    iph = (void *)(eth + 1);
    if ((void *)(iph + 1) > data_end || iph->ihl < 5) {
        return XDP_PASS;
    }

    config = bpf_map_lookup_elem(&xdp_config_map, &key);
    if (!config) {
        return XDP_PASS;
    }

    matched = config->protocol == 0 || config->protocol == iph->protocol;
    if (matched && (iph->protocol == IPPROTO_TCP || iph->protocol == IPPROTO_UDP)) {
        matched = match_l4_port(data_end, iph, config->dst_port);
    }
    if (matched && config->protocol == IPPROTO_ICMP && iph->protocol != IPPROTO_ICMP) {
        matched = false;
    }

    stats = bpf_map_lookup_elem(&xdp_stats_map, &key);
    if (!matched) {
        if (stats) {
            __sync_fetch_and_add(&stats->pass_count, 1);
        }
        return XDP_PASS;
    }

    if (stats) {
        __sync_fetch_and_add(&stats->byte_count, byte_len);
    }
    if (config->action == XDP_POLICY_DROP) {
        if (stats) {
            __sync_fetch_and_add(&stats->drop_count, 1);
        }
        return XDP_DROP;
    }

    if (stats) {
        __sync_fetch_and_add(&stats->pass_count, 1);
    }
    return XDP_PASS;
}
