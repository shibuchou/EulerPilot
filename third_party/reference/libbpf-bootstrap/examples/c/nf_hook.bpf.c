#include "vmlinux.h"
#include "bpf/bpf_helpers.h"
#include "bpf/bpf_tracing.h"
#include "bpf/bpf_core_read.h"
#include <asm/types.h>
#include <linux/version.h>


struct data_t {
    u64 start_time;
    u64 cost_time;
    u64 count;
    u32 pakhash;
};
char LICENSE[] SEC("license") = "Dual BSD/GPL";

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 1024);
    __type(key, pid_t);
    __type(value, u32);
} send_msg SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 1024);
    __type(key, u32);
    __type(value, struct data_t);
} count_map SEC(".maps");

struct {
    //使用ringbuffer（性能远比perfmap,perfbuffer高）
    __uint(type, BPF_MAP_TYPE_RINGBUF); 
    __uint(max_entries, 256 * 1024);
} rb SEC(".maps");

SEC("kprobe/nf_hook_slow")
int BPF_KPROBE(nf_hook_slow_in, struct sk_buff *skb, struct nf_hook_state *state,
		 const struct nf_hook_entries *e, unsigned int s) {
    // 获取当前时间戳
    u64 timestamp = bpf_ktime_get_ns();
    // struct sk_buff *sk = skb;
    u32 pakhash;
    bpf_core_read(&pakhash, sizeof(pakhash), &skb->hash);
    // bpf_printk("---------------------test---------------------------\n");
	// bpf_printk("pakhash: %-8u\n",pakhash);
	// bpf_printk("---------------------test---------------------------\n");
    // 初始化计数器
    struct data_t *data = bpf_map_lookup_elem(&count_map, &pakhash);
    if (data) {
        // 更新统计信息
        data->count += 1;
        data->start_time += timestamp;
        data->pakhash = pakhash;
    } else {
        // 如果没有现有数据，初始化
        struct data_t new_data = { .start_time = timestamp, .count = 1 };
        bpf_map_update_elem(&count_map, &pakhash, &new_data, BPF_ANY);
    }
    int pid = bpf_get_current_pid_tgid();
    bpf_map_update_elem(&send_msg, &pid, &pakhash, BPF_ANY);
    return 0;
}

SEC("kretprobe/nf_hook_slow")
int BPF_KRETPROBE(nf_hook_slow_out, void *ret){
    // 获取当前时间戳
    u64 timestamp = bpf_ktime_get_ns();
    int pid = bpf_get_current_pid_tgid();
    // struct sk_buff *sk; 
    u32 *pakhash = bpf_map_lookup_elem(&send_msg, &pid);  // 获取存储的pakhash
    if (!pakhash) {
        return 0;  // 如果没有找到pakhash，直接返回
    }
    // 初始化计数器
    struct data_t *data = bpf_map_lookup_elem(&count_map, pakhash);
    if (data) {
        // 更新统计信息
        if (timestamp > data->start_time) {
        data->cost_time += timestamp - data->start_time;
        } else {
    // 处理时间戳异常的情况，例如记录警告或重置数据
        bpf_printk("Warning: timestamp is less than start_time\n");
        }
    } else {
        return 0;
    }
    
    struct data_t *mydata;
    mydata = bpf_ringbuf_reserve(&rb, sizeof(*mydata), 0);
    if(!mydata){//错误处理
        return 0;
    }
    //拿数据
    mydata->count = data->count;
    mydata->cost_time = data->cost_time;
    mydata->pakhash = data->pakhash;
    bpf_ringbuf_submit(mydata, 0);
    // bpf_printk("Data submitted: count=%u, cost_time=%llu, pakhash=%u\n", mydata->count, mydata->cost_time, mydata->pakhash);        
    bpf_map_delete_elem(&send_msg, &pid);
    return 0;
}