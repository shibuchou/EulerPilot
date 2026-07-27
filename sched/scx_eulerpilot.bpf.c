/* SPDX-License-Identifier: GPL-2.0 */
#include <scx/common.bpf.h>

char _license[] SEC("license") = "GPL";

enum eulerpilot_class {
	EULERPILOT_CLASS_NORMAL = 0,
	EULERPILOT_CLASS_LATENCY = 1,
	EULERPILOT_CLASS_BATCH = 2,
	EULERPILOT_CLASS_BACKGROUND = 3,
};

enum gate_state {
	GATE_NORMAL = 0,
	GATE_ARMED = 1,
	GATE_ACTIVE = 2,
	GATE_COOLDOWN = 3,
};

struct gate_state_value {
	u32 state;
	u32 generation;
	u64 updated_at_ns;
	u32 evidence_mask;
	u32 reserved;
};

const volatile bool fifo_sched;
const volatile u64 latency_slice_ns = 1000000;
const volatile u64 batch_slice_ns = 6000000;
const volatile u64 background_slice_ns = 2000000;
const volatile u64 shared_slice_ns;
const volatile u64 background_starvation_ns = 20000000;

static u64 vtime_now;
UEI_DEFINE(uei);

#define DSQ_LATENCY  1
#define DSQ_BATCH    2
#define DSQ_BG       3
#define DSQ_SHARED   4

enum stat_idx {
	STAT_CLASS_NORMAL = 0,
	STAT_CLASS_LATENCY = 1,
	STAT_CLASS_BATCH = 2,
	STAT_CLASS_BACKGROUND = 3,
	STAT_CLASSMAP_HIT = 4,
	STAT_CLASSMAP_MISS = 5,
	STAT_INVALID_CLASS = 6,
	STAT_ENQ_SHARED = 7,
	STAT_ENQ_LATENCY = 8,
	STAT_ENQ_BATCH = 9,
	STAT_ENQ_BACKGROUND = 10,
	STAT_DISPATCH_SHARED = 11,
	STAT_DISPATCH_LATENCY = 12,
	STAT_DISPATCH_BATCH = 13,
	STAT_DISPATCH_BACKGROUND = 14,
	STAT_RUNNING_SHARED = 15,
	STAT_RUNNING_LATENCY = 16,
	STAT_RUNNING_BATCH = 17,
	STAT_RUNNING_BACKGROUND = 18,
	STAT_SHARED_FALLBACK = 19,
	STAT_STARVATION_GUARD = 20,
	STAT_BG_CONSUMED_SLICE_TOTAL = 21,
	STAT_DIRECT_LOCAL_LATENCY = 22,
	STAT_DIRECT_LOCAL_BATCH = 23,
	STAT_DIRECT_LOCAL_BACKGROUND = 24,
};

struct bg_ctx {
	u64 last_bg_dispatch_ns;
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 8192);
	__type(key, u32);
	__type(value, u32);
} class_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, u32);
	__type(value, struct gate_state_value);
} gate_state_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(key_size, sizeof(u32));
	__uint(value_size, sizeof(u64));
	__uint(max_entries, 25);
} stats SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(key_size, sizeof(u32));
	__uint(value_size, sizeof(struct bg_ctx));
	__uint(max_entries, 1);
} bg_state SEC(".maps");

static void stat_inc(u32 idx)
{
	u64 *cnt_p = bpf_map_lookup_elem(&stats, &idx);
	if (cnt_p)
		(*cnt_p)++;
}

static u32 lookup_class(struct task_struct *p)
{
	u32 pid = p->tgid;
	u32 *klass = bpf_map_lookup_elem(&class_map, &pid);

	if (klass) {
		stat_inc(STAT_CLASSMAP_HIT);
		if (*klass <= EULERPILOT_CLASS_BACKGROUND)
			return *klass;
		stat_inc(STAT_INVALID_CLASS);
		return EULERPILOT_CLASS_NORMAL;
	}
	stat_inc(STAT_CLASSMAP_MISS);
	return EULERPILOT_CLASS_NORMAL;
}

static u32 current_gate_state(void)
{
	u32 key = 0;
	struct gate_state_value *gate = bpf_map_lookup_elem(&gate_state_map, &key);

	if (!gate)
		return GATE_NORMAL;
	switch (gate->state) {
	case GATE_NORMAL:
	case GATE_ARMED:
	case GATE_ACTIVE:
	case GATE_COOLDOWN:
		return gate->state;
	default:
		return GATE_NORMAL;
	}
}

static bool gate_uses_class_dsqs(u32 state)
{
	return state == GATE_ACTIVE;
}

static u64 select_dsq(u32 klass, u32 gate_state)
{
	if (!gate_uses_class_dsqs(gate_state))
		return DSQ_SHARED;

	switch (klass) {
	case EULERPILOT_CLASS_LATENCY:
		return DSQ_LATENCY;
	case EULERPILOT_CLASS_BATCH:
		return DSQ_BATCH;
	case EULERPILOT_CLASS_BACKGROUND:
		return DSQ_BG;
	default:
		return DSQ_SHARED;
	}
}

static u64 select_slice(u32 klass)
{
	bool active = gate_uses_class_dsqs(current_gate_state());

	if (!active)
		return shared_slice_ns ?: SCX_SLICE_DFL;

	switch (klass) {
	case EULERPILOT_CLASS_LATENCY:
		return latency_slice_ns ?: SCX_SLICE_DFL;
	case EULERPILOT_CLASS_BATCH:
		return batch_slice_ns ?: SCX_SLICE_DFL;
	case EULERPILOT_CLASS_BACKGROUND:
		return background_slice_ns ?: SCX_SLICE_DFL;
	default:
		return shared_slice_ns ?: SCX_SLICE_DFL;
	}
}

static void stat_class_hit(u32 klass)
{
	switch (klass) {
	case EULERPILOT_CLASS_LATENCY:
		stat_inc(STAT_CLASS_LATENCY);
		break;
	case EULERPILOT_CLASS_BATCH:
		stat_inc(STAT_CLASS_BATCH);
		break;
	case EULERPILOT_CLASS_BACKGROUND:
		stat_inc(STAT_CLASS_BACKGROUND);
		break;
	default:
		stat_inc(STAT_CLASS_NORMAL);
		break;
	}
}

static void stat_enqueue_hit(u32 klass)
{
	switch (klass) {
	case EULERPILOT_CLASS_LATENCY:
		stat_inc(STAT_ENQ_LATENCY);
		break;
	case EULERPILOT_CLASS_BATCH:
		stat_inc(STAT_ENQ_BATCH);
		break;
	case EULERPILOT_CLASS_BACKGROUND:
		stat_inc(STAT_ENQ_BACKGROUND);
		break;
	default:
		stat_inc(STAT_ENQ_SHARED);
		break;
	}
}

static void stat_enqueue_dsq(u64 dsq_id, u32 klass)
{
	if (dsq_id == DSQ_SHARED) {
		stat_inc(STAT_ENQ_SHARED);
		return;
	}
	stat_enqueue_hit(klass);
}

static void stat_dispatch_hit(u32 klass)
{
	switch (klass) {
	case EULERPILOT_CLASS_LATENCY:
		stat_inc(STAT_DISPATCH_LATENCY);
		break;
	case EULERPILOT_CLASS_BATCH:
		stat_inc(STAT_DISPATCH_BATCH);
		break;
	case EULERPILOT_CLASS_BACKGROUND:
		stat_inc(STAT_DISPATCH_BACKGROUND);
		break;
	default:
		stat_inc(STAT_DISPATCH_SHARED);
		break;
	}
}

static void stat_running_hit(u32 klass)
{
	switch (klass) {
	case EULERPILOT_CLASS_LATENCY:
		stat_inc(STAT_RUNNING_LATENCY);
		break;
	case EULERPILOT_CLASS_BATCH:
		stat_inc(STAT_RUNNING_BATCH);
		break;
	case EULERPILOT_CLASS_BACKGROUND:
		stat_inc(STAT_RUNNING_BACKGROUND);
		break;
	default:
		stat_inc(STAT_RUNNING_SHARED);
		break;
	}
}

static void stat_add(u32 idx, u64 delta)
{
	u64 *cnt_p = bpf_map_lookup_elem(&stats, &idx);
	if (cnt_p)
		*cnt_p += delta;
}

s32 BPF_STRUCT_OPS(eulerpilot_select_cpu, struct task_struct *p, s32 prev_cpu, u64 wake_flags)
{
	bool is_idle = false;
	s32 cpu;
	u32 klass;

	cpu = scx_bpf_select_cpu_dfl(p, prev_cpu, wake_flags, &is_idle);
	if (is_idle) {
		klass = lookup_class(p);
		if (klass == EULERPILOT_CLASS_LATENCY)
			stat_inc(STAT_DIRECT_LOCAL_LATENCY);
		else if (klass == EULERPILOT_CLASS_BATCH)
			stat_inc(STAT_DIRECT_LOCAL_BATCH);
		else if (klass == EULERPILOT_CLASS_BACKGROUND)
			stat_inc(STAT_DIRECT_LOCAL_BACKGROUND);
		scx_bpf_dsq_insert(p, SCX_DSQ_LOCAL, SCX_SLICE_DFL, 0);
	}

	return cpu;
}

void BPF_STRUCT_OPS(eulerpilot_enqueue, struct task_struct *p, u64 enq_flags)
{
	u32 klass = lookup_class(p);
	u32 gate_state = current_gate_state();
	u64 dsq_id = select_dsq(klass, gate_state);
	u64 slice = select_slice(klass);

	stat_class_hit(klass);
	stat_enqueue_dsq(dsq_id, klass);

	if (fifo_sched || dsq_id != DSQ_SHARED) {
		scx_bpf_dsq_insert(p, dsq_id, slice, enq_flags);
		return;
	}

	if (time_before(p->scx.dsq_vtime, vtime_now - slice))
		p->scx.dsq_vtime = vtime_now - slice;

	scx_bpf_dsq_insert_vtime(p, DSQ_SHARED, slice,
				 p->scx.dsq_vtime, enq_flags);
}

static bool background_starved(void)
{
	u32 zero = 0;
	struct bg_ctx *ctx = bpf_map_lookup_elem(&bg_state, &zero);

	if (!ctx)
		return false;
	if (!ctx->last_bg_dispatch_ns)
		return true;
	return bpf_ktime_get_ns() - ctx->last_bg_dispatch_ns >= background_starvation_ns;
}

static void note_background_dispatch(void)
{
	u32 zero = 0;
	struct bg_ctx *ctx = bpf_map_lookup_elem(&bg_state, &zero);

	if (!ctx)
		return;
	ctx->last_bg_dispatch_ns = bpf_ktime_get_ns();
}

void BPF_STRUCT_OPS(eulerpilot_dispatch, s32 cpu, struct task_struct *prev)
{
	/*
	 * Dispatch must remain able to consume every DSQ in every gate state.
	 * NORMAL/ARMED enqueue new work into DSQ_SHARED, but stale classified
	 * tasks may exist after a gate transition.  COOLDOWN also enqueues new
	 * work into DSQ_SHARED while draining classified leftovers.  This keeps
	 * invalid or missing gate_state_map values fail-safe without stranding
	 * tasks in latency/batch/background DSQs.
	 */

	if (scx_bpf_dsq_move_to_local(DSQ_LATENCY)) {
		stat_dispatch_hit(EULERPILOT_CLASS_LATENCY);
		return;
	}
	if (background_starved() && scx_bpf_dsq_move_to_local(DSQ_BG)) {
		stat_inc(STAT_STARVATION_GUARD);
		stat_dispatch_hit(EULERPILOT_CLASS_BACKGROUND);
		note_background_dispatch();
		return;
	}
	if (scx_bpf_dsq_move_to_local(DSQ_BATCH)) {
		stat_dispatch_hit(EULERPILOT_CLASS_BATCH);
		return;
	}
	if (scx_bpf_dsq_move_to_local(DSQ_BG)) {
		stat_dispatch_hit(EULERPILOT_CLASS_BACKGROUND);
		note_background_dispatch();
		return;
	}

	stat_inc(STAT_SHARED_FALLBACK);
	stat_dispatch_hit(EULERPILOT_CLASS_NORMAL);
	if (scx_bpf_dsq_move_to_local(DSQ_SHARED))
		return;
}

void BPF_STRUCT_OPS(eulerpilot_running, struct task_struct *p)
{
	u32 klass = lookup_class(p);

	stat_running_hit(klass);

	if (fifo_sched)
		return;

	if (time_before(vtime_now, p->scx.dsq_vtime))
		vtime_now = p->scx.dsq_vtime;
}

void BPF_STRUCT_OPS(eulerpilot_stopping, struct task_struct *p, bool runnable)
{
	u32 klass = lookup_class(p);
	u64 consumed;
	u64 slice = select_slice(klass);

	if (fifo_sched)
		return;

	consumed = slice > p->scx.slice ? slice - p->scx.slice : 0;
	if (klass == EULERPILOT_CLASS_BACKGROUND)
		stat_add(STAT_BG_CONSUMED_SLICE_TOTAL, consumed);

	p->scx.dsq_vtime += consumed * 100 / p->scx.weight;
}

void BPF_STRUCT_OPS(eulerpilot_enable, struct task_struct *p)
{
	p->scx.dsq_vtime = vtime_now;
}

s32 BPF_STRUCT_OPS_SLEEPABLE(eulerpilot_init)
{
	s32 ret;

	ret = scx_bpf_create_dsq(DSQ_LATENCY, -1);
	if (ret)
		return ret;
	ret = scx_bpf_create_dsq(DSQ_BATCH, -1);
	if (ret)
		return ret;
	ret = scx_bpf_create_dsq(DSQ_BG, -1);
	if (ret)
		return ret;
	return scx_bpf_create_dsq(DSQ_SHARED, -1);
}

void BPF_STRUCT_OPS(eulerpilot_exit, struct scx_exit_info *ei)
{
	UEI_RECORD(uei, ei);
}

SCX_OPS_DEFINE(eulerpilot_ops,
	       .select_cpu		= (void *)eulerpilot_select_cpu,
	       .enqueue			= (void *)eulerpilot_enqueue,
	       .dispatch		= (void *)eulerpilot_dispatch,
	       .running			= (void *)eulerpilot_running,
	       .stopping		= (void *)eulerpilot_stopping,
	       .enable			= (void *)eulerpilot_enable,
	       .init			= (void *)eulerpilot_init,
	       .exit			= (void *)eulerpilot_exit,
	       .name			= "eulerpilot");
