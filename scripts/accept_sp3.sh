#!/usr/bin/env bash
# SP3 integration acceptance only; this does not establish performance or P1 closure.
# Requires root, an idle sched_ext, and a prior cmake build.
set -euo pipefail

BUILD="${1:?usage: accept_sp3.sh <build-dir> <source-dir>}"
SOURCE="${2:?usage: accept_sp3.sh <build-dir> <source-dir>}"
BUILD="$(realpath -e "$BUILD")"
SOURCE="$(realpath -e "$SOURCE")"

skip() { echo "[skip] $*"; exit 77; }
[ "$(id -u)" = "0" ] || skip "requires root"
[ -x "$BUILD/ep-executord" ] || skip "missing $BUILD/ep-executord"
[ -x "$BUILD/ep" ] || skip "missing $BUILD/ep"
[ -x "$BUILD/ep-sched-loader" ] || skip "missing $BUILD/ep-sched-loader"
[ -x "$BUILD/ep-schedctl" ] || skip "missing $BUILD/ep-schedctl"
[ -d /sys/kernel/sched_ext ] || skip "sched_ext unavailable"
[ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" = "disabled" ] || skip "sched_ext is not idle"
[ -f "$SOURCE/scripts/check_sp3_acceptance.py" ] || skip "missing acceptance validator"
[ -f "$BUILD/CMakeCache.txt" ] || skip "missing build CMakeCache.txt"

# Cooperative lock for acceptance runs. It does not grant ownership of pins
# created by any other process; those are always refused and left untouched.
exec 9>/run/lock/eulerpilot-v2-acceptance.lock
flock -n 9 || skip "another acceptance run holds the lock"
REPORT="$(mktemp -d "$BUILD/acceptance.XXXXXX")"
trap 'rc=$?; if [ "$rc" -ne 0 ]; then echo "[FAIL] acceptance incomplete; evidence retained: $REPORT" >&2; fi' EXIT

python3 - "$BUILD/CMakeCache.txt" "$SOURCE" <<'PY'
from pathlib import Path
import sys
lines = Path(sys.argv[1]).read_text().splitlines()
homes = [line.split('=', 1)[1] for line in lines if line.startswith('CMAKE_HOME_DIRECTORY:INTERNAL=')]
if len(homes) != 1 or Path(homes[0]).resolve() != Path(sys.argv[2]).resolve():
    raise SystemExit('build/source mismatch in CMakeCache.txt')
PY
python3 "$SOURCE/scripts/check_sp3_acceptance.py" idle --phase initial | tee "$REPORT/initial.json"

echo "=== acceptance start $(date -Is) ==="
echo "kernel: $(uname -r)"
echo "build:  $BUILD"
echo "source: $SOURCE"
echo "state:  $(cat /sys/kernel/sched_ext/state)"
echo "evidence: $REPORT"

echo "=== ctest ==="
cd "$BUILD"
ctest --output-on-failure --no-tests=error --output-junit "$REPORT/ctest.xml"
python3 "$SOURCE/scripts/check_sp3_acceptance.py" ctest "$REPORT/ctest.xml" | tee "$REPORT/ctest-check.json"

echo "=== A/B/C liveness ==="
bash "$SOURCE/experiments/abc_nostall.sh" "$BUILD/ep-executord" "$BUILD/ep" \
  "$BUILD/ep-sched-loader" | tee "$REPORT/abc-liveness.txt"

echo "=== hashes ==="
sha256sum ep ep-executord ep-sched-loader ep-schedctl | tee "$REPORT/SHA256SUMS"
sha256sum "$SOURCE/scripts/accept_sp3.sh" "$SOURCE/scripts/check_sp3_acceptance.py" \
  "$SOURCE/experiments/abc_nostall.sh" > "$REPORT/test-source-SHA256SUMS"

echo "=== final state ==="
python3 "$SOURCE/scripts/check_sp3_acceptance.py" idle --phase final | tee "$REPORT/final.json"
echo "=== integration acceptance OK $(date -Is) ==="
echo "This covers the named tests and single-task liveness, not performance, SLO, starvation or P1 closure."
