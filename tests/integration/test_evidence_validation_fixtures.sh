#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT INT TERM HUP

make_throughput_case() {
    local dir="$1"
    local dispatch_total="$2"
    local dispatch_valid="$3"
    mkdir -p "$dir/run-1"
    cat >"$dir/throughput_summary_avg.csv" <<CSV
label,throughput_profile_hits_avg,class_hits_batch_avg,enqueue_batch_avg,batch_dispatch_total_avg,dispatch_accounting_valid_avg,workload_completion_valid_avg
default_batch,0,0,0,0,1,1
cgroup_throughput_first,1,0,0,0,1,1
scx_throughput_first,1,1,3,$dispatch_total,$dispatch_valid,1
CSV
    for label in default_batch cgroup_throughput_first scx_throughput_first; do
        : >"$dir/run-1/${label}_summary.csv"
        echo "$$" >"$dir/run-1/${label}_worker_pids.txt"
        echo "completion_actual=1" >"$dir/run-1/${label}_metrics.env"
    done
    echo snapshot >"$dir/run-1/cgroup_throughput_first_agent_snapshot.txt"
    echo snapshot >"$dir/run-1/scx_throughput_first_agent_snapshot.txt"
}

make_mixed_common_files() {
    local dir="$1"
    mkdir -p "$dir/run-1"
    cat >"$dir/mixed_adaptive_summary.csv" <<CSV
phase,active_seen_count,scheduler_update_evidence_count,switch_latency_ms_avg,recovery_seen_count
quiet_pre,0,0,0,0
pressure_active,1,1,1.0,0
recovery,0,0,0,1
CSV
    for phase in quiet_pre pressure_active recovery; do
        echo summary >"$dir/run-1/${phase}_summary.csv"
        echo snapshot >"$dir/run-1/${phase}_agent_snapshot.txt"
        echo gate >"$dir/run-1/${phase}_gate_status.txt"
        echo trace >"$dir/run-1/${phase}_psi_gate_trace.jsonl"
    done
    echo rollback >"$dir/run-1/rollback_after.log"
}

make_mixed_positive() {
    local dir="$1"
    make_mixed_common_files "$dir"
    cat >"$dir/run-1/combined_psi_gate_trace.jsonl" <<JSONL
{"event_type":"phase_marker","agent_instance_id":"agent-1","event_seq":1,"monotonic_timestamp_ns":100,"phase":"quiet"}
{"event_type":"gate","agent_instance_id":"agent-1","event_seq":2,"monotonic_timestamp_ns":101,"phase":"quiet","gate_state":"NORMAL","next_state":"NORMAL"}
{"event_type":"phase_marker","agent_instance_id":"agent-1","event_seq":3,"monotonic_timestamp_ns":200,"phase":"pressure"}
{"event_type":"gate","agent_instance_id":"agent-1","event_seq":4,"monotonic_timestamp_ns":201,"phase":"pressure","next_state":"ARMED"}
{"event_type":"gate","agent_instance_id":"agent-1","event_seq":5,"monotonic_timestamp_ns":202,"phase":"pressure","next_state":"ACTIVE"}
{"event_type":"phase_marker","agent_instance_id":"agent-1","event_seq":6,"monotonic_timestamp_ns":300,"phase":"recovery"}
{"event_type":"gate","agent_instance_id":"agent-1","event_seq":7,"monotonic_timestamp_ns":301,"phase":"recovery","next_state":"COOLDOWN"}
{"event_type":"gate","agent_instance_id":"agent-1","event_seq":8,"monotonic_timestamp_ns":302,"phase":"recovery","next_state":"NORMAL"}
JSONL
}

make_mixed_negative() {
    local dir="$1"
    make_mixed_common_files "$dir"
    cat >"$dir/run-1/combined_psi_gate_trace.jsonl" <<JSONL
{"event_type":"phase_marker","agent_instance_id":"agent-1","event_seq":1,"monotonic_timestamp_ns":100,"phase":"quiet"}
{"event_type":"gate","agent_instance_id":"agent-1","event_seq":2,"monotonic_timestamp_ns":101,"phase":"quiet","next_state":"NORMAL"}
{"event_type":"phase_marker","agent_instance_id":"agent-2","event_seq":1,"monotonic_timestamp_ns":200,"phase":"pressure"}
{"event_type":"gate","agent_instance_id":"agent-2","event_seq":2,"monotonic_timestamp_ns":201,"phase":"pressure","next_state":"ACTIVE"}
JSONL
}

make_throughput_case "$TMP_DIR/throughput-first-positive" 3 1
make_throughput_case "$TMP_DIR/throughput-first-negative" 0 0
make_mixed_positive "$TMP_DIR/mixed-adaptive-positive"
make_mixed_negative "$TMP_DIR/mixed-adaptive-negative"

python3 "$ROOT_DIR/scripts/collect_final_evidence.py" --validate-run "$TMP_DIR/throughput-first-positive" >/dev/null
if python3 "$ROOT_DIR/scripts/collect_final_evidence.py" --validate-run "$TMP_DIR/throughput-first-negative" >/dev/null 2>&1; then
    echo "negative throughput fixture unexpectedly passed" >&2
    exit 1
fi
python3 "$ROOT_DIR/scripts/collect_final_evidence.py" --validate-run "$TMP_DIR/mixed-adaptive-positive" >/dev/null
if python3 "$ROOT_DIR/scripts/collect_final_evidence.py" --validate-run "$TMP_DIR/mixed-adaptive-negative" >/dev/null 2>&1; then
    echo "negative mixed fixture unexpectedly passed" >&2
    exit 1
fi

echo "evidence_validation_fixtures=pass"
