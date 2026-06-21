#include "vmlinux.h"

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

char LICENSE[] SEC("license") = "GPL";

#ifndef EPERM
#define EPERM 1
#endif

#define TARGET_PATH "/root/EulerPilot/demo/security_policy_demo/secret.txt"

struct security_policy_config {
    __u32 enforce;
};

struct security_policy_event {
    __u32 pid;
    __u32 tgid;
    __u32 enforce;
    __s32 decision;
    char comm[16];
    char path[256];
};

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct security_policy_config);
} policy_map SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 1 << 20);
} events SEC(".maps");

SEC("lsm/file_open")
int BPF_PROG(security_policy_demo, struct file *file, int ret)
{
    if (ret != 0)
        return ret;

    char path[256];
    long len = bpf_d_path(&file->f_path, path, sizeof(path));
    if (len <= 0)
        return 0;

    for (int i = 0; i < sizeof(TARGET_PATH) - 1; i++) {
        if (path[i] != TARGET_PATH[i])
            return 0;
    }
    if (path[sizeof(TARGET_PATH) - 1] != '\0')
        return 0;

    __u32 key = 0;
    struct security_policy_config *config = bpf_map_lookup_elem(&policy_map, &key);
    __u32 enforce = config ? config->enforce : 1;
    __s32 decision = enforce ? -EPERM : 0;

    struct security_policy_event *event =
        bpf_ringbuf_reserve(&events, sizeof(*event), 0);
    if (event) {
        __u64 pid_tgid = bpf_get_current_pid_tgid();
        event->pid = (__u32)pid_tgid;
        event->tgid = (__u32)(pid_tgid >> 32);
        event->enforce = enforce;
        event->decision = decision;
        bpf_get_current_comm(&event->comm, sizeof(event->comm));
        __builtin_memcpy(event->path, path, sizeof(event->path));
        bpf_ringbuf_submit(event, 0);
    }

    return decision;
}
