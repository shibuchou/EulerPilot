// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include "workload_observer.h"
#include "workload_observer.skel.h"

static volatile sig_atomic_t exiting;

static void sig_handler(int sig)
{
    (void)sig;
    exiting = 1;
}

static int dump_metrics(struct workload_observer_bpf *skel)
{
    __u32 key = 0, next_key;
    struct task_metrics metrics;
    int map_fd = bpf_map__fd(skel->maps.task_metrics_map);

    printf("%-7s %-7s %-16s %-16s %-8s %-10s %-12s %-10s %-10s %-6s\n",
           "PID", "TGID", "COMM", "START_BOOT_NS", "WAKEUPS", "WAIT_NS",
           "RUNTIME_NS", "CTXSW", "MIGRATE", "CPU");

    while (bpf_map_get_next_key(map_fd, &key, &next_key) == 0) {
        if (bpf_map_lookup_elem(map_fd, &next_key, &metrics) == 0) {
            printf("%-7u %-7u %-16s %-16lu %-8lu %-10lu %-12lu %-10lu %-10lu %-6u\n",
                   metrics.pid, metrics.tgid, metrics.comm,
                   metrics.start_boottime_ns,
                   metrics.wakeup_count, metrics.total_wait_ns,
                   metrics.runtime_ns, metrics.ctx_switch_count,
                   metrics.migrate_count, metrics.last_cpu);
        }
        key = next_key;
    }

    return 0;
}

int main(void)
{
    struct workload_observer_bpf *skel;
    int err;

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    libbpf_set_strict_mode(LIBBPF_STRICT_ALL);

    skel = workload_observer_bpf__open();
    if (!skel) {
        fprintf(stderr, "failed to open workload observer skeleton\n");
        return 1;
    }

    err = workload_observer_bpf__load(skel);
    if (err) {
        fprintf(stderr, "failed to load workload observer skeleton: %d\n", err);
        workload_observer_bpf__destroy(skel);
        return 1;
    }

    err = workload_observer_bpf__attach(skel);
    if (err) {
        fprintf(stderr, "failed to attach workload observer skeleton: %d\n", err);
        workload_observer_bpf__destroy(skel);
        return 1;
    }

    while (!exiting) {
        printf("---- workload observer snapshot ----\n");
        dump_metrics(skel);
        sleep(1);
    }

    workload_observer_bpf__destroy(skel);
    return 0;
}
