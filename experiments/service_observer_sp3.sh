#!/usr/bin/env bash
# EulerPilot v2 -- resident service observer with lost-observation accounting.
#
# Wiring the SLO gate also wires a dependency: the gate refuses to act once a
# measurement is older than two of its windows, so a producer that dies silently
# turns the automatic channel into a no-op that looks like "suppression is working".
# This producer therefore reports its own health from both sides -- it counts what
# it managed to ingest, and the executor counts what it accepted and rejected, and
# the two numbers have to agree.
#
#   service_observer_sp3.sh <ep> <socket> <probe> <out-dir> [window_s] [interval_s] [duration_s]
#
# Each cycle reads the expert that is in place, runs the foreground and background
# probes together, and ingests one observation. Transient failures are retried and
# counted, never fatal; a cycle whose expert is unknown (a switch is in flight) is
# skipped rather than mislabelled. On exit it writes observer.summary.json.
set -uo pipefail

EP="${1:?usage: service_observer_sp3.sh <ep> <socket> <probe> <out-dir> [window_s] [interval_s] [duration_s]}"
SOCK="${2:?missing socket path}"
PROBE="${3:?missing service_load_probe}"
OUT="${4:?missing out dir}"
WINDOW_S="${5:-15}"
INTERVAL_S="${6:-15}"
DURATION_S="${7:-120}"
WORKLOAD="${WORKLOAD:-svc-resident}"
RETRIES="${RETRIES:-2}"

[ -x "$PROBE" ] || { echo "missing probe: $PROBE"; exit 1; }
mkdir -p "$OUT"
CYCLE=0
INGESTED=0
SKIPPED=0
PROBE_FAILS=0
INGEST_FAILS=0
STOP=0
PIDS=()
: > "$OUT/observer.errors"
: > "$OUT/observer.csv"
echo "cycle,observed_at_ms,window_ms,expert,p99_ms,bg_units,bg_wait_ms,ack" >> "$OUT/observer.csv"

now_ms() { python3 -c 'import time;print(int(time.time()*1000))'; }
LAST_OK_MS=$(now_ms)
START_MS=$LAST_OK_MS
MAX_GAP_MS=0
TOTAL_WINDOW_MS=0

finish() {
  local elapsed_ms
  elapsed_ms=$(( $(now_ms) - START_MS ))
  python3 - "$OUT/observer.summary.json" "$CYCLE" "$INGESTED" "$SKIPPED" "$PROBE_FAILS" \
    "$INGEST_FAILS" "$TOTAL_WINDOW_MS" "$elapsed_ms" "$MAX_GAP_MS" "$WINDOW_S" <<'SUMMARY'
import json, sys
(path, attempted, ingested, skipped, probe_fails, ingest_fails, windowed, elapsed,
 longest_gap, window_s) = sys.argv[1:11]
attempted, ingested = int(attempted), int(ingested)
elapsed = int(elapsed)
summary = {
  "cycles_attempted": attempted,
  "ingested": ingested,
  "skipped_no_policy": int(skipped),
  "probe_failures": int(probe_fails),
  "ingest_failures": int(ingest_fails),
  "window_s": int(window_s),
  "elapsed_ms": elapsed,
  "measured_ms": int(windowed),
  # coverage: how much of the run was actually described by an accepted measurement.
  "coverage_pct": round(100.0 * int(windowed) / elapsed, 2) if elapsed else 0.0,
  "longest_gap_ms": int(longest_gap),
}
json.dump(summary, open(path, "w"), indent=2, sort_keys=True)
print("OBSERVER_SUMMARY " + json.dumps(summary, sort_keys=True))
SUMMARY
}

on_stop() { STOP=1; }
trap on_stop TERM INT

while [ "$STOP" = "0" ]; do
  now=$(now_ms)
  [ "$now" -ge $((START_MS + DURATION_S * 1000)) ] && break
  CYCLE=$((CYCLE + 1))
  status=$("$EP" status --socket "$SOCK" 2>>"$OUT/observer.errors") || {
    INGEST_FAILS=$((INGEST_FAILS + 1)); echo "cycle=$CYCLE status failed" >> "$OUT/observer.errors"; sleep 1; continue; }
  expert=$(printf '%s' "$status" |
           python3 -c "import json,sys;print(json.load(sys.stdin)['auto_apply'].get('active_expert') or '')")
  if [ -z "$expert" ] && ! printf '%s' "$status" | grep -q '"auto_apply"'; then
    echo "cycle=$CYCLE status unreadable: $(printf '%s' "$status" | head -c 120)" >> "$OUT/observer.errors"
    INGEST_FAILS=$((INGEST_FAILS + 1))
    sleep 1
    continue
  fi
  if [ -z "$expert" ]; then
    # A switch is in flight. Skipping is honest; labelling the window with the
    # previous effect would feed the gate a measurement of a state it never saw.
    SKIPPED=$((SKIPPED + 1))
    echo "cycle=$CYCLE skipped (no policy in place)" >> "$OUT/observer.errors"
    sleep 1
    continue
  fi

  attempt=0
  merged=""
  while [ "$attempt" -lt "$RETRIES" ]; do
    attempt=$((attempt + 1))
    "$PROBE" fg "$expert" "$WINDOW_S" 1000 > "$OUT/fg-$CYCLE.json" 2>>"$OUT/observer.errors" &
    PIDS+=("$!")
    if "$PROBE" bg "$expert" "$WINDOW_S" > "$OUT/bg-$CYCLE.json" 2>>"$OUT/observer.errors"; then
      bg_ok=1
    else
      bg_ok=0
    fi
    for pid in "${PIDS[@]:-}"; do wait "$pid" 2>/dev/null; done
    PIDS=()
    if [ "$bg_ok" = "1" ] && merged=$(python3 - "$OUT/fg-$CYCLE.json" "$OUT/bg-$CYCLE.json" \
         "$OUT/obs-$CYCLE.json" "$WORKLOAD" 2>>"$OUT/observer.errors" <<'MERGE'
import json, sys
fg = json.load(open(sys.argv[1]))
bg = json.load(open(sys.argv[2]))
start = min(fg["observed_at_ms"] - fg["window_ms"], bg["observed_at_ms"] - bg["window_ms"])
end = max(fg["observed_at_ms"], bg["observed_at_ms"])
obs = {"workload": sys.argv[4], "observed_at_ms": end,
       "window_ms": max(100, end - start), "expert": fg["expert"],
       "foreground": {"p99_latency_ms": fg["p99_latency_ms"]},
       "background": {"work_units_per_sec": bg["work_units_per_sec"],
                      "max_wait_ms": bg["max_wait_ms"]}}
json.dump(obs, open(sys.argv[3], "w"))
print(sys.argv[3])
MERGE
    ); then
      break
    fi
    echo "cycle=$CYCLE probe attempt $attempt failed" >> "$OUT/observer.errors"
    PROBE_FAILS=$((PROBE_FAILS + 1))
    sleep 1
  done
  [ -n "$merged" ] || continue

  if "$EP" observe --file "$merged" --socket "$SOCK" > "$OUT/ack-$CYCLE.json" 2>>"$OUT/observer.errors"; then
    ack=OK
  else
    ack=REJECTED
    INGEST_FAILS=$((INGEST_FAILS + 1))
    echo "cycle=$CYCLE ingest rejected" >> "$OUT/observer.errors"
    continue
  fi

  INGESTED=$((INGESTED + 1))
  this_ms=$(now_ms)
  gap=$((this_ms - LAST_OK_MS))
  [ "$gap" -gt "$MAX_GAP_MS" ] && MAX_GAP_MS=$gap
  LAST_OK_MS=$this_ms
  printf '%s\n' "$this_ms" > "$OUT/observer.heartbeat.tmp" && mv "$OUT/observer.heartbeat.tmp" "$OUT/observer.heartbeat"
  python3 - "$CYCLE" "$merged" "$OUT/ack-$CYCLE.json" "$ack" >> "$OUT/observer.csv" <<'ROW'
import json, sys
cycle, obs_path, ack_path, status = sys.argv[1:5]
obs = json.load(open(obs_path))
ack = json.load(open(ack_path))
outcome = ack.get("switch_outcome") or {}
print(",".join(str(c) for c in (cycle, obs["observed_at_ms"], obs["window_ms"], obs["expert"],
                                "%.3f" % obs["foreground"]["p99_latency_ms"],
                                "%.1f" % obs["background"]["work_units_per_sec"],
                                "%.3f" % obs["background"]["max_wait_ms"],
                                "%s evaluated=%s hurt=%s code=%s" % (status, outcome.get("evaluated"),
                                                                     outcome.get("hurt"),
                                                                     outcome.get("code", "")))))
ROW
  TOTAL_WINDOW_MS=$((TOTAL_WINDOW_MS + $(python3 -c "import json;print(json.load(open('$merged'))['window_ms'])")))
  sleep "$INTERVAL_S"
done
finish
