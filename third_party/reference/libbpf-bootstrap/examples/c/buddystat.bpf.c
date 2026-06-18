#include "vmlinux.h"
#include "bpf/bpf_helpers.h"
#include "bpf/bpf_tracing.h"
#include "bpf/bpf_core_read.h"
#define MAX_ORDER 11

char LICENSE[] SEC("license") = "Dual BSD/GPL";

struct mydata_t {
    int pid;
    int cpu;
    u64 alloc_time;
    u64 free_time;
    unsigned int order;
    unsigned long num0;
    unsigned long num1;
    unsigned long num2;
    unsigned long num3;
    unsigned long num4;
    unsigned long num5;
    unsigned long num6;
    unsigned long num7;
    unsigned long num8;
    unsigned long num9;
    unsigned long num10;
    int migratetype;
	unsigned long count;
    unsigned int alloc_flags;
    struct zone *zone;
    struct free_area free_area[MAX_ORDER];
};

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct mydata_t);
} heap SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size, sizeof(pid_t));
    __uint(value_size, sizeof(int));
} myevents SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1024);
	__type(key, int);
	__type(value, struct mydata_t);
} buddy1 SEC(".maps");


SEC("kprobe/__rmqueue_pcplist")
int BPF_KPROBE(__rmqueue_pcplist, struct zone *zone, unsigned int order,
			// unsigned long count, struct list_head *list,
			int migratetype, unsigned int alloc_flags){
    struct mydata_t *d;
    int zero = 0;
    d = bpf_map_lookup_elem(&heap, &zero);
	if (!d) /* can't happen */
		return 0;
    int cpu;
    cpu = bpf_get_smp_processor_id();
    d->cpu = cpu;
    pid_t pid;
    pid = bpf_get_current_pid_tgid() >> 32;
    u64 alloc_time;
    alloc_time = bpf_ktime_get_ns();
    d->pid = pid;
    d->alloc_time = alloc_time;
    d->zone = zone;
    bpf_probe_read_str(&d->free_area[0], sizeof(zone->free_area[0]), &zone->free_area[0]);
    d->num0 = d->free_area[0].nr_free;
    bpf_probe_read_str(&d->free_area[1], sizeof(zone->free_area[1]), &zone->free_area[1]);
    d->num1 = d->free_area[1].nr_free;
    bpf_probe_read_str(&d->free_area[2], sizeof(zone->free_area[2]), &zone->free_area[2]);
    d->num2 = d->free_area[2].nr_free;
    bpf_probe_read_str(&d->free_area[3], sizeof(zone->free_area[3]), &zone->free_area[3]);
    d->num3 = d->free_area[3].nr_free;
    bpf_probe_read_str(&d->free_area[4], sizeof(zone->free_area[4]), &zone->free_area[4]);
    d->num4 = d->free_area[4].nr_free;
    bpf_probe_read_str(&d->free_area[5], sizeof(zone->free_area[5]), &zone->free_area[5]);
    d->num5 = d->free_area[5].nr_free;
    bpf_probe_read_str(&d->free_area[6], sizeof(zone->free_area[6]), &zone->free_area[6]);
    d->num6 = d->free_area[6].nr_free;
    bpf_probe_read_str(&d->free_area[7], sizeof(zone->free_area[7]), &zone->free_area[7]);
    d->num7 = d->free_area[7].nr_free;
    bpf_probe_read_str(&d->free_area[8], sizeof(zone->free_area[8]), &zone->free_area[8]);
    d->num8 = d->free_area[8].nr_free;
    bpf_probe_read_str(&d->free_area[9], sizeof(zone->free_area[9]), &zone->free_area[9]);
    d->num9 = d->free_area[9].nr_free;
    bpf_probe_read_str(&d->free_area[10], sizeof(zone->free_area[10]), &zone->free_area[10]);
    d->num10 = d->free_area[10].nr_free;
    // d->count = count;
    d->order = order;
    d->migratetype = migratetype;
    bpf_map_update_elem(&buddy1, &migratetype, d, BPF_ANY);
    bpf_perf_event_output(ctx, &myevents, BPF_F_CURRENT_CPU, d, sizeof(*d));
    return 0;
       
}