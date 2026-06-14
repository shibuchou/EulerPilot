#include "vmlinux.h"

#include <bpf/bpf_endian.h>
#include <bpf/bpf_helpers.h>

char LICENSE[] SEC("license") = "GPL";

const volatile __u16 deny_port = 18080;

SEC("cgroup/connect4")
int network_policy_demo(struct bpf_sock_addr *ctx)
{
    if (bpf_ntohs(ctx->user_port) == deny_port) {
        return 0;
    }
    return 1;
}
