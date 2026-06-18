#include "vmlinux.h"    //内置（基于Linux5.8）
#include "bpf/bpf_helpers.h"    //内核和ebpf子系统沟通使用的函数
#include "bpf/bpf_tracing.h"
#include "common.h"

char LICENSE[] SEC("license") = "Dual BSD/GPL";

//定义MAP
struct {
    //使用ringbuffer（性能远比perfmap,perfbuffer高）
    __uint(type, BPF_MAP_TYPE_RINGBUF); 
    __uint(max_entries, 256 * 1024);
} rb SEC(".maps");

//声明许可（内核强制要求）


//定义挂载点
SEC("kprobe/update_rq_clock")
//定义hook函数内容，操作
int BPF_KPROBE(update_rq_clock,struct rq* rq){
    struct mydata_t* mydata;
    //预留ringbuf空间
    mydata = bpf_ringbuf_reserve(&rb, sizeof(*mydata), 0);
    if(!mydata){//错误处理
        return 0;
    }
    //拿数据
    bpf_probe_read_str(&mydata->__nr_running, sizeof(rq->nr_running), &rq->nr_running); //其他的先不管
    mydata->__cpu = bpf_get_smp_processor_id();

    bpf_ringbuf_submit(mydata, 0);
    return 0;
}
