#!/usr/bin/env bash
# EulerPilot v2 -- observation-channel watchdog.
#
# The SLO gate consumes measurements it cannot produce, so a producer that dies
# quietly turns the automatic channel into a no-op that reads like "the pilot is
# being careful". This script closes that hole from the consumer side. It polls
# the control plane's own accounting rather than the producer's log, which is the
# only reason it can still see a gap after the gap has ended: age_ms exists while
# someone asks, but observation_max_age_ms and stale_checks are remembered.
#
#   service_watchdog_sp3.sh <ep> <socket> <out-dir> [poll_s] [duration_s] \
#                           [heartbeat_file] [observer_csv]
#
# Any breach is logged as soon as it is seen and the script exits nonzero at the
# end, so it can be dropped into a run as a witness without being the thing that
# decides the outcome. The staleness threshold is the definition the gate itself
# uses: a measurement older than two of its own windows.
set -uo pipefail

EP="${1:?usage: service_watchdog_sp3.sh <ep> <socket> <out-dir> [poll_s] [duration_s] [heartbeat] [observer.csv]}"
SOCK="${2:?missing socket path}"
OUT="${3:?missing out dir}"
POLL_S="${4:-5}"
DURATION_S="${5:-120}"
HEARTBEAT="${6:-}"
OBS_CSV="${7:-}"
# How long the channel may be dark before an operator should hear about it.
ALERT_MAX_AGE_MS="${ALERT_MAX_AGE_MS:-60000}"
# A heartbeat file older than this means the producer, not the transport, is gone.
HEARTBEAT_LIMIT_MS="${HEARTBEAT_LIMIT_MS:-60000}"

mkdir -p "$OUT"
: > "$OUT/watchdog.alerts"
: > "$OUT/watchdog.errors"
echo "poll,observed_at_ms,age_ms,window_ms,accepted,rejected,stale_checks,max_age_ms,heartbeat_age_ms,verdict" \
  > "$OUT/watchdog.csv"

now_ms() { python3 -c 'import time;print(int(time.time()*1000))'; }

START_MS=$(now_ms)
END_MS=$((START_MS + DURATION_S * 1000))
STOP=0
POLL=0
BREACHES=0
STATUS_FAILS=0
PREV_REJECTED=""
PREV_STALE=""
MAX_AGE_SEEN=0
MAX_HEARTBEAT_AGE="NA"

on_stop() { STOP=1; }
trap on_stop TERM INT

alert() {
  BREACHES=$((BREACHES + 1))
  printf '%s %s\n' "$(now_ms)" "$1" >> "$OUT/watchdog.alerts"
  echo "ALERT $1" >&2
}

finish() {
  python3 - "$OUT/watchdog.summary.json" "$POLL" "$BREACHES" "$STATUS_FAILS" "$MAX_AGE_SEEN" \
    "$MAX_HEARTBEAT_AGE" "$ALERT_MAX_AGE_MS" "$HEARTBEAT_LIMIT_MS" <<'SUMMARY'
import json, sys
(path, polls, breaches, status_fails, max_age, hb_max, alert_age, hb_limit) = sys.argv[1:9]
summary = {
    "polls": int(polls),
    "breaches": int(breaches),
    "status_failures": int(status_fails),
    "max_observation_age_ms_seen": int(max_age),
    "alert_max_age_ms": int(alert_age),
    "heartbeat_limit_ms": int(hb_limit),
    "max_heartbeat_age_ms": int(hb_max) if hb_max.isdigit() else None,
    "verdict": "clean" if int(breaches) == 0 else "breached",
}
json.dump(summary, open(path, "w"), indent=2, sort_keys=True)
print("WATCHDOG_SUMMARY " + json.dumps(summary, sort_keys=True))
SUMMARY
}

while [ "$STOP" = "0" ]; do
  [ "$(now_ms)" -ge "$END_MS" ] && break
  POLL=$((POLL + 1))
  verdict=OK
  status=$("$EP" status --socket "$SOCK" 2>>"$OUT/watchdog.errors") || {
    STATUS_FAILS=$((STATUS_FAILS + 1))
    alert "NO_CONTROL_PLANE control plane did not answer status (poll $POLL)"
    sleep "$POLL_S"
    continue
  }

  parsed=$(printf '%s' "$status" | python3 -c '
import json, sys
doc = json.load(sys.stdin)
s = doc.get("service", {})
a = doc.get("auto_apply", {})
print("\t".join(str(x) for x in (
    "yes" if s.get("observed") else "no",
    s.get("age_ms", 0), s.get("window_ms", 0),
    s.get("observations_accepted", 0), s.get("observations_rejected", 0),
    s.get("stale_checks", 0), s.get("observation_max_age_ms", 0),
    s.get("last_rejection", "").replace("\t", " "),
    a.get("active_expert", ""))))
') || { alert "BAD_REPLY status reply was not parseable (poll $POLL)"; sleep "$POLL_S"; continue; }
  IFS=$'\t' read -r observed age window accepted rejected stale max_age last_rejection active \
    <<< "$parsed"

  [ "$max_age" -gt "$MAX_AGE_SEEN" ] && MAX_AGE_SEEN=$max_age

  # Each rule is a different failure mode, so each says which one it saw.
  if [ -n "$PREV_STALE" ] && [ "$stale" -gt "$PREV_STALE" ]; then
    verdict=STALE_EPISODE
    alert "STALE_EPISODE channel went stale $((stale - PREV_STALE)) time(s) between polls (high-water ${max_age}ms)"
  fi
  if [ "$max_age" -gt "$ALERT_MAX_AGE_MS" ]; then
    verdict=STALE_MEMORY
    alert "STALE_MEMORY the channel was dark for at least ${max_age}ms (alert limit ${ALERT_MAX_AGE_MS}ms)"
  fi
  if [ "$observed" = "yes" ] && [ "$window" -gt 0 ] && [ "$age" -gt $((2 * window)) ]; then
    verdict=STALE_NOW
    alert "STALE_NOW the live measurement is already stale: age ${age}ms > 2 x window ${window}ms"
  fi
  if [ -n "$PREV_REJECTED" ] && [ "$rejected" -gt "$PREV_REJECTED" ]; then
    verdict=REJECTIONS
    alert "REJECTIONS control plane refused $((rejected - PREV_REJECTED)) more observation(s): ${last_rejection}"
  fi
  if [ -n "$active" ] && [ "$observed" = "no" ]; then
    verdict=NO_EVIDENCE
    alert "NO_EVIDENCE a policy ($active) is in place but no service measurement has ever been ingested"
  fi

  hb_age=NA
  if [ -n "$HEARTBEAT" ] && [ -f "$HEARTBEAT" ]; then
    hb_mtime=$(stat -c %Y "$HEARTBEAT" 2>/dev/null || echo 0)
    hb_age=$(( $(now_ms) - hb_mtime * 1000 ))
    if [ "$hb_age" -gt "$HEARTBEAT_LIMIT_MS" ]; then
      verdict=PRODUCER_SILENT
      alert "PRODUCER_SILENT producer heartbeat is ${hb_age}ms old (limit ${HEARTBEAT_LIMIT_MS}ms)"
    fi
    if [ "$MAX_HEARTBEAT_AGE" = "NA" ] || [ "$hb_age" -gt "$MAX_HEARTBEAT_AGE" ]; then
      MAX_HEARTBEAT_AGE=$hb_age
    fi
  fi

  # Two independent counters have to agree, or one of them is lying about the channel.
  if [ -n "$OBS_CSV" ] && [ -f "$OBS_CSV" ]; then
    rows=$(( $(grep -c ',' "$OBS_CSV") - 1 ))
    if [ "$rows" -ne "$accepted" ]; then
      verdict=ACCOUNTING
      alert "ACCOUNTING producer logged $rows accepted ingest(s) but the control plane counts $accepted"
    fi
  fi

  printf '%s\n' "$POLL,$(now_ms),$age,$window,$accepted,$rejected,$stale,$max_age,$hb_age,$verdict" \
    >> "$OUT/watchdog.csv"
  PREV_REJECTED=$rejected
  PREV_STALE=$stale
  sleep "$POLL_S"
done
finish
[ "$BREACHES" = "0" ] || { echo "WATCHDOG BREACHED ($BREACHES)"; exit 1; }
echo "WATCHDOG CLEAN ($POLL polls, max observation age ${MAX_AGE_SEEN}ms)"
