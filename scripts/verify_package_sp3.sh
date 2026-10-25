#!/usr/bin/env bash
# EulerPilot v2 -- package verification: the frozen binaries in a build tree
# must be byte-identical to the hashes recorded when the evidence was produced,
# and the evidence files that back the claims must exist in the source tree.
#
#   verify_package_sp3.sh <build-dir> <src-dir>
#
# Reads the hash table from evidence/p1/frozen-build-20261025.txt. This is the
# last gate before handing anything to a judge: no hash match, no claim.
set -euo pipefail

BUILD="${1:?usage: verify_package_sp3.sh <build-dir> <src-dir>}"
SRC="${2:?}"

fails=0
check() { # <expected16> <file> <label>
  local want=$1 file=$2 label=$3 have=""
  if [ -f "$file" ]; then have="$(sha256sum "$file" | cut -c1-16)"; fi
  if [ "$want" = "$have" ]; then
    echo "ok    $label ($have)"
  else
    echo "FAIL  $label: expected $want, got ${have:-missing}"
    fails=$((fails + 1))
  fi
}

echo "== frozen binaries (evidence/p1/frozen-build-20261025.txt) =="
check 3693c9ac22dfdea2 "$BUILD/ep-executord"   "ep-executord"
check 3cb202fd138ed167 "$BUILD/ep"             "ep"
check 2752282392b50f9b "$BUILD/ep-evtmon"      "ep-evtmon"
check e33496d46b2ca287 "$BUILD/ep-sched-loader" "ep-sched-loader"
check 6fef4d43a5cf7f30 "$BUILD/ep-schedctl"    "ep-schedctl"
check 433653b1d03299fd "$SRC/sched/events_trace.bpf.o" "events_trace.bpf.o (rebuildable)"

echo "== evidence files behind the claims =="
for f in \
  evidence/p1/frozen-build-20261025.txt \
  evidence/p1/fail-safe-20261024.txt \
  evidence/p1/class-drift-20261025.txt \
  evidence/p4/cgroup-domain-20261024.txt \
  evidence/p4/events-link-20261025.txt \
  evidence/p2/flagship-matrix-20261023.txt \
  evidence/p2/flagship-rerun-20261025.txt \
  evidence/p2/barms-20261025.txt \
  evidence/p2/earms-20261025.txt \
  evidence/p2/mixed-fg-noclass-20261025.txt \
  evidence/p5/demo-rehearsals-20261025.txt \
  docs/MANUAL_SP3.md docs/HANDOVER_20261024.md PROVENANCE.md LICENSE NOTICE; do
  if [ -f "$SRC/$f" ]; then echo "ok    $f"; else echo "FAIL  missing $f"; fails=$((fails + 1)); fi
done

echo "== design contracts =="
if python3 -c "import jsonschema" 2>/dev/null; then
  if python3 "$SRC/scripts/validate_design.py" >/dev/null 2>&1; then
    echo "ok    validate_design.py (6 positive / 27 negative)"
  else
    echo "FAIL  validate_design.py"
    fails=$((fails + 1))
  fi
else
  echo "skip  validate_design.py (jsonschema not installed here; run on a host that has it)"
fi

if [ "$fails" -ne 0 ]; then
  echo "PACKAGE_VERIFY: $fails failure(s)"
  exit 1
fi
echo "PACKAGE_VERIFY: all checks passed"
