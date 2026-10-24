#!/usr/bin/env bash
# EulerPilot v2 -- is the load generator the bottleneck? (protocol 6.1 "client 饱和")
#
# The pilot used redis-benchmark, which is closed-loop and gave sub-second bursts, so
# offered load dropped whenever the service slowed down. The open-loop probe fixes the
# arrival side, but a single-threaded generator saturates somewhere too, and past that
# point the numbers describe the client, not the system. This sweep finds that ceiling
# on this machine, with no scheduler involved, before any arm comparison is trusted.
#
#   redis_rate_calibration_sp3.sh [out-dir] [seconds-per-point]
#
# Needs redis-server and a built redis_openloop_probe. Manual.
set -uo pipefail

OUT="${1:-/root/epv2-evidence/rate-calibration}"
SECONDS_PER_POINT="${2:-20}"
HERE="$(cd "$(dirname "$0")" && pwd)"
PROBE="$HERE/redis_openloop_probe"
PORT="${PORT:-7797}"
CONNS="${CONNS:-8}"
KEYPOOL="${KEYPOOL:-100000}"
RATES="${RATES:-5000 10000 20000 40000 80000}"
RUN=/tmp/ep-ratecal

[ -x "$PROBE" ] || { echo "missing $PROBE"; exit 1; }
command -v redis-server >/dev/null || { echo "redis-server missing"; exit 77; }
mkdir -p "$OUT"; rm -f "$OUT/calibration.csv"
echo "point,rate_rps,offered,sent,completed,good,bad,timeouts,stalls,peak_inflight,achieved_rps,goodput_rps,deficit_rps,cpu_per_second,raw_bytes" > "$OUT/calibration.csv"

REDIS_PID=""
cleanup() {
  local rc=$?
  [ -n "$REDIS_PID" ] && kill -TERM "$REDIS_PID" 2>/dev/null
  wait "$REDIS_PID" 2>/dev/null
  echo "cleanup rc=$rc"
}
trap cleanup EXIT

redis-server --port "$PORT" --daemonize no --save '' --appendonly no --io-threads 1 \
  --maxmemory 512mb --logfile "$OUT/redis.log" &
REDIS_PID=$!
for _ in $(seq 1 100); do redis-cli -p "$PORT" ping >/dev/null 2>&1 && break; sleep 0.2; done
redis-cli -p "$PORT" ping >/dev/null || { echo "redis did not start"; exit 1; }

# Populate exactly the keypool the probe reads (k0..k<keypool-1>); a mismatch makes GET
# measure a miss path and the arm comparison silently changes meaning.
echo "populating $KEYPOOL keys through the probe's own key naming"
"$PROBE" "$CONNS" 40000 $((KEYPOOL / 40000 + 2)) SET --port "$PORT" --keypool "$KEYPOOL" \
  --timeout-ms 500 --raw "$RUN-unused.raw" > "$OUT/populate.json" 2>&1
redis-cli -p "$PORT" dbsize > "$OUT/redis.dbsize" 2>&1
echo "dbsize=$(cat "$OUT/redis.dbsize")"
rm -f "$RUN-unused.raw"

point=0
for rate in $RATES; do
  for op in GET SET; do
    point=$((point + 1))
    echo "### point=$point op=$op rate=$rate $(date +%T)"
    out=$("$PROBE" "$CONNS" "$rate" "$SECONDS_PER_POINT" "$op" --port "$PORT" \
          --keypool "$KEYPOOL" --slo-ms 5 --timeout-ms 100 --raw "$OUT/raw-$point.bin" 2>/dev/null)
    echo "$out" > "$OUT/point-$point.json"
    python3 - "$point" "$rate" "$out" "$OUT/calibration.csv" "$OUT/raw-$point.bin" <<'PY' >> "$OUT/calibration.csv"
import json, os, sys
point, rate, raw_text, csv_path, raw_file = sys.argv[1:6]
try:
    d = json.loads(raw_text)
except Exception:
    d = {}
row = [point, rate, d.get("offered", 0), d.get("sent", 0), d.get("completed", 0),
       d.get("good", 0), d.get("bad", 0), d.get("timeouts", 0), d.get("stalls", 0),
       d.get("peak_inflight", 0), "%.1f" % d.get("achieved_rps", 0.0),
       "%.1f" % d.get("goodput_at_slo_rps", 0.0), "%.1f" % d.get("deficit_rps", 0.0),
       "%.3f" % d.get("cpu_per_second", 0.0),
       os.path.getsize(raw_file) if os.path.exists(raw_file) else 0]
print(",".join(str(r) for r in row))
PY
  done
done

python3 - "$OUT/calibration.csv" <<'VERDICT'
import csv, sys
rows = list(csv.DictReader(open(sys.argv[1])))
print("op-independent view (per rate, GET then SET):")
print("rate      op   achieved/offered  goodput@5ms  cpu_per_s  timeouts  stalls")
clean = []
for r in rows:
    rate = float(r["rate_rps"])
    ratio = float(r["completed"]) / float(r["offered"]) if int(r["offered"]) else 0
    print("%-9s %-4s %6.2f%%          %-11s %-10s %-8s %s" % (
        r["rate_rps"], "GET" if int(r["point"]) % 2 else "SET", 100 * ratio,
        r["goodput_rps"], r["cpu_per_second"], r["timeouts"], r["stalls"]))
    if ratio >= 0.995 and float(r["cpu_per_second"]) < 0.90 and int(r["timeouts"]) == 0:
        clean.append(rate)
print()
if clean:
    print("usable without client-side bottleneck (achieved>=99.5%% of offered, cpu<0.90, no timeouts):")
    print("  up to %s rps at conns=8" % int(max(clean)))
else:
    print("no rate met the client-bottleneck criteria; the generator itself is the limit")
print("NOTE this is generator capacity on this VM, not a performance result.")
VERDICT
sha256sum "$OUT"/*.json "$OUT"/raw-*.bin > "$OUT/SHA256SUMS" 2>/dev/null
