#include "vmlinux.h"
#include "bpf/bpf_helpers.h"
#include "bpf/bpf_tracing.h"
#include "bpf/bpf_core_read.h"
#define NSEC_PER_USEC	1000L

char LICENSE[] SEC("license") = "Dual BSD/GPL";

struct my_data {
    int pid;
	int full;
    unsigned long avg0;
	unsigned long avg1;
	unsigned long avg2;
	u64 total;
};

enum psi_res {
	PSI_IO,
	PSI_MEM,
	PSI_CPU,
	NR_PSI_RESOURCES = 3,
};

enum psi_states {
	PSI_IO_SOME,
	PSI_IO_FULL,
	PSI_MEM_SOME,
	PSI_MEM_FULL,
	PSI_CPU_SOME,
	/* Only per-CPU, to weigh the CPU in the global average: */
	PSI_NONIDLE,
	NR_PSI_STATES = 6,
};

enum psi_aggregators {
	PSI_AVGS = 0,
	PSI_POLL,
	NR_PSI_AGGREGATORS,
};

struct psigroup {
	/* Protects data used by the aggregator */
	struct mutex avgs_lock;

	/* Total stall times and sampled pressure averages */
	u64 total[NR_PSI_AGGREGATORS][NR_PSI_STATES - 1];
	unsigned long avg[NR_PSI_STATES - 1][3];

};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1024);
	__type(key, pid_t);
	__type(value,u64);
}psid SEC(".maps");

struct   {
    __uint(type ,BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size , sizeof(pid_t));
    __uint(value_size , sizeof(int));
}events_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct my_data);
} heap SEC(".maps");


SEC("kprobe/update_averages")
int BPF_KPROBE(update_averages, struct psi_group *group, u64 now) {
    struct my_data *d;
	struct psigroup *g;
	bpf_core_read(&g, sizeof(group), &group);
    int zero = 0;
    d = bpf_map_lookup_elem(&heap, &zero);
	if (!d) /* can't happen */
		return 0;

    pid_t pid;
    pid = bpf_get_current_pid_tgid() >> 32;
    d->pid = pid;
    int full;
	int res = PSI_CPU;
	for (full = 0; full < 2 - (res == PSI_CPU); full++) {
	unsigned long avg[3];
	u64 total;
	int w;
	for (w = 0; w < 3; w++)
	bpf_probe_read_str(&avg[w], sizeof(g->avg[res * 2 + full][w]), &g->avg[res * 2 + full][w]);
	//avg[w] = group->avg[res * 2 + full][w];
	bpf_probe_read(&d->total, sizeof(g->total[PSI_AVGS][res * 2 + full]), &g->total[PSI_AVGS][res * 2 + full]);
	//total = div_u64(group->total[PSI_AVGS][res * 2 + full],NSEC_PER_USEC);
	d->avg0 = avg[0];
	d->avg1 = avg[1];
	d->avg2 = avg[2];
	d->full = full;
	//LOAD_INT(avg[0]), LOAD_FRAC(avg[0]),
	//LOAD_INT(avg[1]), LOAD_FRAC(avg[1]),
	//LOAD_INT(avg[2]), LOAD_FRAC(avg[2]),
	d->total = total;
	}

    bpf_map_update_elem(&psid, &d->total, d, BPF_ANY);
    bpf_perf_event_output(ctx, &events_map, BPF_F_CURRENT_CPU, d, sizeof(*d));

    return 0;
}

