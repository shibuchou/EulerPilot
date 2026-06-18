#include "vmlinux.h"

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

char LICENSE[] SEC("license") = "GPL";

#ifndef EPERM
#define EPERM 1
#endif

#define TARGET_PATH "/root/EulerPilot/demo/security_policy_demo/secret.txt"

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

    return -EPERM;
}
