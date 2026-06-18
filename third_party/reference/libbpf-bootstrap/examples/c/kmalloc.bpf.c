#include "vmlinux.h"
#include "bpf/bpf_helpers.h"
#include "bpf/bpf_tracing.h"
#include "bpf/bpf_core_read.h"


#include "ebpf.h"
#include "memleak.h"

char LICENSE[] SEC("license") = "Dual BSD/GPL";

struct kmalloc_t {
    struct trace_entry ent;
	long unsigned int call_site;
	const void *ptr;
	size_t bytes_req;
	size_t bytes_alloc;
	long unsigned int gfp_flags;
	int node;
	char __data[0];
};

struct kfree_t {
    struct trace_entry ent;
	long unsigned int call_site;
	const void *ptr;
	char __data[0];
};



struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 8192);
    __type(key, void* );  // 使用地址作为key
    __type(value, struct kmalloc_t);
} alloc_info SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 8192);
    __type(key, void* );  // 使用地址作为key
    __type(value, struct kfree_t);
} free_info SEC(".maps");

struct   {
    __uint(type ,BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size , sizeof(pid_t));
    __uint(value_size , sizeof(int));
}perf_event SEC(".maps");

SEC("tracepoint/kmem/kmalloc")
int kmalloc_entry(struct trace_event_raw_kmalloc *ctx) {
    
    struct task_struct *curr = (struct task_struct *)bpf_get_current_task();
    u32 tgid = BPF_CORE_READ(curr, tgid);
    size_t bytes_alloc = ctx->bytes_alloc;
    const void *ptr = ctx->ptr;
    
    struct kmalloc_t *kmalloc_data;
    kmalloc_data = bpf_map_lookup_elem(&alloc_info, ptr);
    if(!kmalloc_data){
        return 0;
    }
    kmalloc_data->ent.pid = tgid;
    kmalloc_data->call_site = ctx->call_site;
    kmalloc_data->ptr = ptr;
    kmalloc_data->bytes_req = ctx->bytes_req;
    kmalloc_data->bytes_alloc = bytes_alloc;
    kmalloc_data->gfp_flags = ctx->gfp_flags;
    kmalloc_data->node = ctx->node;

    bpf_map_update_elem(&alloc_info, &ptr, &kmalloc_data, BPF_ANY);
    bpf_perf_event_output(ctx, &perf_event, BPF_F_CURRENT_CPU, &kmalloc_data, sizeof(kmalloc_data));
    return 0;
    
}


// SEC("tracepoint/kmem/kfree")
// int kfree_entry(struct trace_event_raw_kfree *ctx) {
//     struct task_struct *curr = (struct task_struct *)bpf_get_current_task();


// }