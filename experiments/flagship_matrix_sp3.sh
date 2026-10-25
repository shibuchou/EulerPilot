#!/usr/bin/env bash
# EulerPilot v2 -- the flagship run, driven by a frozen parameter file.
#
# Protocol 6.2 requires the sample size, warmup and measure window to be fixed before
# the run, and 5.x requires the BG floor / max-wait thresholds to be registered after
# the pilot. Both of those live in one place: flagship_params.json. Nothing here
# invents a number; the script reads the file, echoes what it is about to run, and
# refuses if the file is missing or the box is not idle.
#
#   flagship_matrix_sp3.sh <ep-executord> <ep> <ep-sched-loader> <out-dir>
#
# Produces <out-dir>/<block>-<arm>/ (the matrix's own layout), plus protocol.json
# (preregistration, including the hash of the parameter file it was run under),
# attempts.jsonl (every block/arm, valid or not, with the reason) and the paired
# table from analyze_matrix_blocks.py. Pilot data never enters this directory.
set -uo pipefail

EXECUTORD="${1:?usage: flagship_matrix_sp3.sh <ep-executord> <ep> <ep-sched-loader> <out-dir>}"
EP="${2:?missing ep}"
LOADER="${3:?missing ep-sched-loader}"
OUT="${4:?missing out dir}"
HERE="$(cd "$(dirname "$0")" && pwd)"
MATRIX="$HERE/redis_matrix_sp3.sh"
PARAMS="${PARAMS:-$HERE/flagship_params.json}"

[ -f "$MATRIX" ] || { echo "missing $MATRIX"; exit 1; }
[ -f "$PARAMS" ] || { echo "missing frozen parameter file: $PARAMS"; exit 1; }
[ "$(id -u)" = 0 ] || { echo "requires root"; exit 77; }
[ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" = "disabled" ] || { echo "sched_ext not idle"; exit 1; }

mkdir -p "$OUT"
# One generated env file rather than a shell read of a spaced field: arms contain
# spaces, and "read -r a b c" would split them into the wrong variables.
python3 - "$PARAMS" "$OUT/frozen.env" <<'READ'
import json, shlex, sys
p = json.load(open(sys.argv[1]))
w, slo, gate, analysis = p["workload"], p["slo"], p["online_gate"], p["analysis"]
values = {
    "BLOCKS": p["blocks"], "ARMS": " ".join(p["arms"]), "BASELINE": p["baseline"],
    "RATE": w["offered_rps"], "CONNS": w["conns"], "KEYPOOL": w["keypool"],
    "WARMUP": w["warmup_s"], "MEASURE": w["measure_s"], "TIMEOUT": w["timeout_ms"],
    "POPULATE": w["populate_requests"], "SLO": slo["threshold_ms"],
    "ALSO_SLO": ",".join(str(x) for x in slo.get("reported_also_at_ms", [])),
    "BG_FLOOR": gate["min_bg_work_units_per_sec"], "BG_WAIT": gate["max_bg_wait_ms"],
    "FG_CEILING": gate["max_foreground_p99_ms"], "BG_WINDOW": gate["bg_window_s"],
    "SEED": p["seed"], "RESAMPLES": analysis["bootstrap_resamples"],
}
with open(sys.argv[2], "w") as out:
    for key, value in values.items():
        out.write("%s=%s\n" % (key, shlex.quote(str(value))))
READ
# shellcheck disable=SC1090
. "$OUT/frozen.env"
echo "frozen: blocks=$BLOCKS arms='$ARMS' baseline=$BASELINE offered=$RATE rps slo=${SLO}ms"
echo "        measure=${MEASURE}s warmup=${WARMUP}s conns=$CONNS keypool=$KEYPOOL populate=$POPULATE"
echo "gate:   bg_floor=$BG_FLOOR units/s max_wait=$BG_WAIT ms fg_p99_ceiling=$FG_CEILING ms bg_window=${BG_WINDOW}s"

python3 - "$PARAMS" "$OUT/protocol.json" "$HERE/redis_matrix_sp3.sh" "$HERE/redis_openloop_probe" \
  "$HERE/service_load_probe" "$EXECUTORD" "$EP" "$LOADER" <<'PREREG'
import hashlib, json, os, platform, subprocess, sys
(params_path, out, harness, probe, svc, executord, ep, loader) = sys.argv[1:9]
def sha(f):
    return hashlib.sha256(open(f, "rb").read()).hexdigest()
params = json.load(open(params_path))
# Preregistration has to be checkable after the fact: the file's own hash is recorded,
# so a parameter edited between the pilot and the run cannot pass as frozen.
json.dump({
  "experiment": "flagship-noisy-cpu-redis",
  "preregistered_at": subprocess.check_output(["date", "-Is"]).decode().strip(),
  "parameters_sha256": sha(params_path),
  "parameters": params,
  "statistics": {"comparison": "paired within block", "baseline": params["baseline"],
                 "ci": "paired block bootstrap over per-block paired differences",
                 "resamples": params["analysis"]["bootstrap_resamples"],
                 "seed": params["seed"], "level": 0.95},
  "invalidity": {"block_invalid": "block.invalid present, or raw records disagree with summary",
                 "attach_required": ["C", "D"], "never_dropped_for": ["low throughput", "high variance"]},
  "host": {"kernel": platform.release(), "cpus": os.cpu_count(),
           "machine": platform.node()},
  "harness_sha256": {"redis_matrix_sp3.sh": sha(harness), "redis_openloop_probe": sha(probe),
                     "service_load_probe": sha(svc), "ep-executord": sha(executord),
                     "ep": sha(ep), "ep-sched-loader": sha(loader)},
}, open(out, "w"), indent=2, sort_keys=True)
print("wrote %s" % out)
PREREG

BLOCKS="$BLOCKS" ARMS="$ARMS" RATE="$RATE" CONNS="$CONNS" KEYPOOL="$KEYPOOL" \
WARMUP_S="$WARMUP" MEASURE_S="$MEASURE" SLO_MS="$SLO" TIMEOUT_MS="$TIMEOUT" \
BG_FLOOR_UNITS="$BG_FLOOR" BG_MAX_WAIT_MS="$BG_WAIT" FG_P99_CEILING_MS="$FG_CEILING" \
BG_WINDOW_S="$BG_WINDOW" POPULATE_REQUESTS="$POPULATE" SEED="$SEED" \
  bash "$MATRIX" "$EXECUTORD" "$EP" "$LOADER" "$OUT"
matrix_rc=$?
echo "matrix rc=$matrix_rc"

python3 - "$OUT" <<'LEDGER'
import json, os, sys
root = sys.argv[1]
path = os.path.join(root, "attempts.jsonl")
count = 0
with open(path, "w") as out:
    for entry in sorted(os.listdir(root)):
        block_dir = os.path.join(root, entry)
        if not os.path.isdir(block_dir) or "-" not in entry:
            continue
        block, arm = entry.split("-", 1)
        invalid = os.path.join(block_dir, "block.invalid")
        record = {"block": block, "arm": arm,
                  "valid": not os.path.exists(invalid),
                  "reason": open(invalid).read().strip() if os.path.exists(invalid) else None,
                  "attached": (open(os.path.join(block_dir, "schedext.state")).read().strip()
                               if os.path.exists(os.path.join(block_dir, "schedext.state"))
                               else "missing"),
                  "has_raw": os.path.exists(os.path.join(block_dir, "raw-get.bin")) and
                             os.path.exists(os.path.join(block_dir, "raw-set.bin"))}
        out.write(json.dumps(record, sort_keys=True) + "\n")
        count += 1
        if not record["valid"]:
            print("INVALID %s: %s" % (entry, record["reason"]))
print("attempts recorded: %d -> %s" % (count, path))
LEDGER

python3 "$HERE/analyze_matrix_blocks.py" "$OUT" --baseline "$BASELINE" --arms "$ARMS" \
  --threshold "$SLO" --also-thresholds "$ALSO_SLO" --measure-s "$MEASURE" --seed "$SEED" \
  --resamples "$RESAMPLES" \
  | tee "$OUT/analysis.txt"
echo "flagship run complete: $OUT (matrix rc=$matrix_rc)"
