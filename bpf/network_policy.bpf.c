#include "vmlinux.h"

#include <bpf/bpf_endian.h>
#include <bpf/bpf_helpers.h>

char LICENSE[] SEC("license") = "GPL";

/* Minimal cgroup/connect4 program for the formal network_policy Skill.
 * User space owns target resolution, audit, and rollback; the BPF path only
 * enforces the configured destination port and maintains hit counters.
 */
struct network_policy_config {
    __u16 deny_port;
    __u8 enforce;
    __u8 reserved[5];
};

struct network_policy_stats {
    __u64 allow_count;
    __u64 deny_count;
};

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct network_policy_config);
} policy_map SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct network_policy_stats);
} stats_map SEC(".maps");

SEC("cgroup/connect4")
int network_policy_connect4(struct bpf_sock_addr *ctx)
{
    __u32 key = 0;
    struct network_policy_config *config;
    struct network_policy_stats *stats;
    __u16 deny_port = 18080;
    __u8 enforce = 1;

    config = bpf_map_lookup_elem(&policy_map, &key);
    if (config) {
        if (config->deny_port) {
            deny_port = config->deny_port;
        }
        enforce = config->enforce;
    }

    stats = bpf_map_lookup_elem(&stats_map, &key);
    if (bpf_ntohs(ctx->user_port) == deny_port) {
        if (stats) {
            __sync_fetch_and_add(&stats->deny_count, 1);
        }
        if (!enforce) {
            return 1;
        }
        return 0;
    }
    if (stats) {
        __sync_fetch_and_add(&stats->allow_count, 1);
    }
    return 1;
}
