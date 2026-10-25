#!/usr/bin/env bash
# EulerPilot v2 -- freeze the flagship parameters from measurement, not from habit.
#
# Protocol 6.2 says the flagship run's sample size, warmup and measure window are set
# up front, and 5.x says the BG floor and the longest runnable wait are registered
# *after* a pilot. Three things have never been measured on this box:
#
#   * which offered rate leaves the bare service (A0) with real headroom -- at 80k the
#     server itself queues (calibration), and a table where every arm is at the server
#     ceiling cannot show a scheduling effect at all;
#   * which SLO threshold separates the arms rather than being always-met or
#     always-broken -- answered from the per-request raw records, so one run gives the
#     answer for every candidate threshold;
#   * what the background work unit actually measures, and how much it moves between
#     arms -- the online gate declares a floor in the same unit, so a floor of 20/s
#     against a measured 165/s is a number nobody would defend.
#
#   flagship_param_pilot_sp3.sh <ep-executord> <ep> <ep-sched-loader> [out-dir]
#
# Stage 1 runs A0 (bare redis) once per candidate rate. Stage 2 runs the arms in
# ARMS_STAGE2 at CANDIDATE_RATE. Pilot blocks never enter the formal table.
set -uo pipefail

EXECUTORD="${1:?usage: flagship_param_pilot_sp3.sh <ep-executord> <ep> <ep-sched-loader> [out-dir]}"
EP="${2:?missing ep}"
LOADER="${3:?missing ep-sched-loader}"
OUT="${4:-/root/epv2-evidence/flagship-pilot}"
HERE="$(cd "$(dirname "$0")" && pwd)"
MATRIX="$HERE/redis_matrix_sp3.sh"
RATES="${RATES:-20000 40000 60000}"
ARMS_STAGE2="${ARMS_STAGE2:-A0 C D}"
CANDIDATE_RATE="${CANDIDATE_RATE:-40000}"
BLOCKS_STAGE1="${BLOCKS_STAGE1:-1}"
BLOCKS_STAGE2="${BLOCKS_STAGE2:-1}"
CONNS="${CONNS:-8}"
KEYPOOL="${KEYPOOL:-100000}"
WARMUP_S="${WARMUP_S:-10}"
MEASURE_S="${MEASURE_S:-60}"
SLO_MS="${SLO_MS:-5}"
THRESHOLDS="${THRESHOLDS:-1,2,5,10}"
SEED="${SEED:-20261024}"

[ -f "$MATRIX" ] || { echo "missing $MATRIX"; exit 1; }
mkdir -p "$OUT"

LOCK_FILE=/run/lock/eulerpilot-v2-matrix.lock
wait_for_lock() {  # the previous block's teardown can outlive its own shell
  for _ in $(seq 1 30); do
    flock -n "$LOCK_FILE" -c true 2>/dev/null && return 0
    sleep 2
  done
  return 1
}

run_matrix() {  # out-dir label
  local dir=$1 label=$2 log="$OUT/$2.matrix.log" rc
  rm -rf "$dir"
  BLOCKS="$BLOCKS_CUR" ARMS="$ARMS_CUR" RATE="$RATE_CUR" CONNS="$CONNS" KEYPOOL="$KEYPOOL" \
  WARMUP_S="$WARMUP_S" MEASURE_S="$MEASURE_S" SLO_MS="$SLO_MS" SEED="$SEED" \
    bash "$MATRIX" "$EXECUTORD" "$EP" "$LOADER" "$dir" >"$log" 2>&1
  rc=$?
  if grep -q "holds the lock" "$log"; then
    echo "FAILED $label: the matrix refused to start (lock held)"
    return 1
  fi
  [ -d "$dir" ] || { echo "FAILED $label: no evidence dir produced"; return 1; }
  tail -6 "$log"
  return 0
}

echo "=== stage 1: offered-rate scan on the bare service (A0 only) ==="
# RATES is whitespace-only when the caller only wants stage 2; `${RATES:-default}`
# cannot express "no rates", so the scan is gated on stripped content instead.
if [ "${RATES// /}" != "" ]; then
for rate in $RATES; do
  BLOCKS_CUR="$BLOCKS_STAGE1" ARMS_CUR="A0" RATE_CUR="$rate"
  wait_for_lock || { echo "lock still held after 60s; aborting"; exit 1; }
  run_matrix "$OUT/rate-$rate" "rate-$rate" || exit 1
done
else
  echo "(skipped: RATES is empty)"
fi

echo "=== stage 2: arms at candidate rate $CANDIDATE_RATE ==="
BLOCKS_CUR="$BLOCKS_STAGE2" ARMS_CUR="$ARMS_STAGE2" RATE_CUR="$CANDIDATE_RATE"
wait_for_lock || { echo "lock still held after 60s; aborting"; exit 1; }
run_matrix "$OUT/arms-$CANDIDATE_RATE" "arms-$CANDIDATE_RATE" || exit 1

echo "=== latency distributions rebuilt from the per-request raw records ==="
for rate in $RATES; do
  echo "--- rate $rate (A0) ---"
  python3 "$HERE/analyze_openloop_raw.py" "$OUT/rate-$rate" --thresholds "$THRESHOLDS"
done
echo "--- arms at $CANDIDATE_RATE ---"
python3 "$HERE/analyze_openloop_raw.py" "$OUT/arms-$CANDIDATE_RATE" --thresholds "$THRESHOLDS"

echo "=== background work unit: what one window of the probe actually is ==="
python3 - "$OUT" <<'BGUNIT'
import json, os, statistics, sys
root = sys.argv[1]
for entry in sorted(os.listdir(root)):
    block_dir = os.path.join(root, entry)
    marks = os.path.join(block_dir, "bg.marks")
    proof = os.path.join(block_dir, "populate.proof")
    if not os.path.exists(marks):
        continue
    vals = [float(x) for x in open(marks).read().split()]
    rows = []
    bg = os.path.join(block_dir, "bg.ndjson")
    if os.path.exists(bg):
        for line in open(bg, errors="replace"):
            line = line.strip()
            if line.startswith("{"):
                try:
                    rows.append(json.loads(line))
                except Exception:
                    pass
    waits = [r.get("max_wait_ms") for r in rows if isinstance(r.get("max_wait_ms"), (int, float))]
    units = [r.get("work_units_per_sec") for r in rows if isinstance(r.get("work_units_per_sec"), (int, float))]
    spread = (max(units) - min(units)) / statistics.median(units) * 100 if len(units) > 1 else 0.0
    print("%s windows=%d units/s median=%s spread=%0.1f%% wait max=%s | %s" % (
        entry, len(rows),
        "%.1f" % statistics.median(units) if units else "none",
        spread,
        "%.1f" % max(waits) if waits else "none",
        open(proof).read().strip() if os.path.exists(proof) else "no populate proof"))
BGUNIT
