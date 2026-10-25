#!/usr/bin/env bash
# Quantify the R5 pump's own cost (evidence for the boundary noted in
# events-link-20261025.txt §5).
#
# Three measurements, one box, same redis load:
#   1. pump CPU time: ep-evtmon's own rusage over a fixed window, with the
#      workload running, plus the BPF-side counters (wakeups counted, discards,
#      lost updates) from the pump's own reports.
#   2. paired service impact: identical open-loop probes with the pump OFF and
#      ON (same window, alternating), paired goodput/p99 per pair.
#   3. the executor-side counters while ingesting (observations accepted).
#
#   pump_overhead_sp3.sh <ep-executord> <ep> <ep-evtmon> [out-dir]
set -euo pipefail

EXECUTORD="${1:?usage: pump_overhead_sp3.sh <ep-executord> <ep> <ep-evtmon> [out]}"
EP="${2:?}"
EVTMON="${3:?}"
OUT="${4:-/root/epv2-evidence/pump-overhead-$(date +%Y%m%d)}"
HERE="$(cd "$(dirname "$0")" && pwd)"
PROBE="$HERE/redis_openloop_probe"

skip() { echo "[skip] $*"; exit 77; }
[ "$(id -u)" = "0" ] || skip "requires root"
[ "$(stat -fc %T /sys/fs/cgroup)" = "cgroup2fs" ] || skip "cgroup v2 required"
[ -x "$EVTMON" ] || skip "ep-evtmon missing"
[ -x "$PROBE" ] || skip "redis_openloop_probe missing"
command -v redis-server >/dev/null || skip "redis-server required"
# The pump does not need sched_ext; this experiment never attaches a scheduler.

mkdir -p "$OUT"
STATE="$OUT/state"
SOCK="$STATE/executor.sock"
PORT=7799
PAIRS="${PAIRS:-6}"
RATE="${RATE:-40000}"
PUMP_PID=""
REDIS_PID=""

cleanup() {
  local rc=$?
  [ -n "$PUMP_PID" ] && kill -TERM "$PUMP_PID" 2>/dev/null || true
  [ -n "${EXEC_PID:-}" ] && kill -TERM "${EXEC_PID:-}" 2>/dev/null || true
  if [ -n "$REDIS_PID" ] && kill -0 "$REDIS_PID" 2>/dev/null; then
    redis-cli -p "$PORT" shutdown nosave >/dev/null 2>&1 || kill -TERM "$REDIS_PID" 2>/dev/null || true
  fi
  wait 2>/dev/null || true
  echo "[pump-overhead] cleanup rc=$rc"
}
trap cleanup EXIT
fail() { echo "[FAIL] $*"; exit 1; }

redis-server --port "$PORT" --daemonize no --save '' --appendonly no --io-threads 1 \
  --logfile "$OUT/redis.log" >"$OUT/redis.stdout" 2>&1 9>&- &
REDIS_PID=$!
"$PROBE" 4 "$RATE" 5 GET --port "$PORT" --keypool 50000 --slo-ms 5 --timeout-ms 50 \
  --raw "$OUT/warmup.bin" >"$OUT/warmup.json" 2>/dev/null || fail "warmup probe failed"

# The executor (mock skill: no scheduler, no cgroup writes) exists only so the
# pump has a real event.observe consumer -- the ingest path is part of the cost.
"$EXECUTORD" --state-dir "$STATE" --socket "$SOCK" --skill mock >"$OUT/executor.log" 2>&1 9>&- &
EXEC_PID=$!
for _ in $(seq 1 100); do "$EP" status --socket "$SOCK" >/dev/null 2>&1 && break; sleep 0.1; done

pump_report_stat() { # <json-key> -- from the pump's last report line
  grep -o "\"$2\":[0-9]*" "$1" 2>/dev/null | tail -1 | cut -d: -f2
}

echo "[pump-overhead] phase 1: paired service impact, pump OFF vs ON ($PAIRS pairs, ${RATE} rps)"
for pair in $(seq 1 "$PAIRS"); do
  # OFF leg
  "$PROBE" 4 "$RATE" 10 GET --port "$PORT" --keypool 50000 --slo-ms 5 --timeout-ms 50 \
    --raw "$OUT/p$pair-off.bin" >"$OUT/p$pair-off.json" 2>>"$OUT/probe.err"
  # ON leg: start the pump (2s window like the online gate), probe, stop, keep stats
  "$EVTMON" --socket "$SOCK" --window-ms 2000 --interval-ms 500 >"$OUT/p$pair-pump.log" 2>&1 9>&- &
  PUMP_PID=$!
  sleep 1
  "$PROBE" 4 "$RATE" 10 GET --port "$PORT" --keypool 50000 --slo-ms 5 --timeout-ms 50 \
    --raw "$OUT/p$pair-on.bin" >"$OUT/p$pair-on.json" 2>>"$OUT/probe.err"
  kill -TERM "$PUMP_PID" 2>/dev/null || true
  wait "$PUMP_PID" 2>/dev/null || true
  PUMP_PID=""
  echo "pair $pair done" >> "$OUT/pairs.progress"
done

echo "[pump-overhead] phase 2: pump rusage over a longer window under the same load"
"$EVTMON" --socket "$SOCK" --window-ms 2000 --interval-ms 500 >"$OUT/long-pump.log" 2>&1 9>&- &
PUMP_PID=$!
"$PROBE" 4 "$RATE" 60 GET --port "$PORT" --keypool 50000 --slo-ms 5 --timeout-ms 50 \
  --raw "$OUT/long-load.bin" >"$OUT/long-load.json" 2>>"$OUT/probe.err"
# pump CPU time (utime+stime in clock ticks) read right before the TERM
awk '{print "utime_ticks=" $14 " stime_ticks=" $15 " threads=" $20}'   "/proc/$PUMP_PID/stat" > "$OUT/pump-rusage.txt" 2>/dev/null || true
getconf CLK_TCK > "$OUT/clk-tck.txt" 2>/dev/null || echo 100 > "$OUT/clk-tck.txt"
kill -TERM "$PUMP_PID" 2>/dev/null || true
wait "$PUMP_PID" 2>/dev/null || true
PUMP_PID=""

python3 - "$OUT" <<'REPORT'
import glob, json, os, statistics as st, struct, sys
root = sys.argv[1]

def doc(path):
    return json.load(open(path))

def lat_ms(raw):
    blob = open(raw, "rb").read()
    vals = sorted(struct.unpack("<%dQ" % (len(blob) // 8), blob))
    return [v / 1e6 for v in vals]

def p99(xs):
    return xs[max(0, int(len(xs) * 0.99) - 1)]

def good_at_slo(d, xs, slo=5.0):
    return sum(1 for x in xs if x <= slo) / ((d["finished_at_ms"] - d["offered_at_ms"]) / 1000.0)

pairs = []
for off in sorted(glob.glob(os.path.join(root, "p*-off.json"))):
    on = off.replace("-off.json", "-on.json")
    if not os.path.exists(on):
        continue
    d_off, d_on = doc(off), doc(on)
    x_off, x_on = lat_ms(off.replace(".json", ".bin")), lat_ms(on.replace(".json", ".bin"))
    pairs.append({
        "off_good": good_at_slo(d_off, x_off), "on_good": good_at_slo(d_on, x_on),
        "off_p99": p99(x_off), "on_p99": p99(x_on),
        "off_achieved": d_off["achieved_rps"], "on_achieved": d_on["achieved_rps"],
    })

gains = [100.0 * (p["on_good"] / p["off_good"] - 1) for p in pairs if p["off_good"] > 0]
p99d = [100.0 * (p["on_p99"] / p["off_p99"] - 1) for p in pairs if p["off_p99"] > 0]
print("pairs=%d goodput(on/off-1): median %.3f%% range [%.3f, %.3f]"
      % (len(pairs), st.median(gains), min(gains), max(gains)))
print("        p99(on/off-1):      median %.1f%% range [%.1f, %.1f]"
      % (st.median(p99d), min(p99d), max(p99d)))

# pump self report from the long window
last = ""
for line in open(os.path.join(root, "long-pump.log"), errors="replace"):
    if line.startswith("{") and chr(34)+"pump"+chr(34)+":" in line:
        last = line
try:
    r = json.loads(last)
    pump = r.get("pump", {})
    print("pump window report: wakeups=%s waits_counted=%s pid_reuse_discards=%s agg_updates_lost=%s reports=%s"
          % (pump.get("wakeups"), pump.get("waits_counted"), pump.get("pid_reuse_discards"),
             pump.get("agg_updates_lost"), pump.get("reports")))
except Exception as e:
    print("pump report unreadable:", e)

try:
    parts = open(os.path.join(root, "pump-rusage.txt")).read().split()
    utime, stime = float(parts[0].split("=")[1]), float(parts[1].split("=")[1])
    clk = float(open(os.path.join(root, "clk-tck.txt")).read().strip())
    d = json.load(open(os.path.join(root, "long-load.json")))
    secs = (d["finished_at_ms"] - d["offered_at_ms"]) / 1000.0
    print("pump cpu: utime+stime %.2fs over %.0fs load window = %.1f%% of one core"
          % ((utime + stime) / clk, secs, 100.0 * (utime + stime) / clk / secs))
except Exception as e:
    print("pump rusage unreadable:", e)

# executor ingest counters: see $OUT/executor-status.json (printed by the shell)
REPORT

"$EP" status --socket "$SOCK" > "$OUT/executor-status.json" 2>&1 || true
echo "[pump-overhead] artifacts in $OUT"
