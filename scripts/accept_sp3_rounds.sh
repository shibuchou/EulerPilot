#!/usr/bin/env bash
# EulerPilot v2 -- run scripts/accept_sp3.sh N times and keep each round's
# evidence under a name that says which round set it belongs to.
#
# This exists because the r19 round set was driven by a sed-derived copy that
# still carried the previous round's name, so six logs had to be reconstructed
# from timestamps and directory contents. The naming is the product of this
# script; nothing else about a round is decided here.
#
#   accept_sp3_rounds.sh <round-tag> <build-dir> <source-dir> [rounds]
#
# Writes <build-dir>/<tag>-roundN/ (the evidence dir) and
# <build-dir>/../epv2-evidence/acc-<tag>-N.txt unless EVIDENCE_DIR overrides it.
# Exits nonzero as soon as a round fails, because a green tail after a red round
# is how a regression gets hidden.
set -uo pipefail

TAG="${1:?usage: accept_sp3_rounds.sh <round-tag> <build-dir> <source-dir> [rounds]}"
BUILD="$(realpath -e "${2:?missing build dir}")"
SOURCE="$(realpath -e "${3:?missing source dir}")"
ROUNDS="${4:-6}"
EVIDENCE_DIR="${EVIDENCE_DIR:-$(dirname "$BUILD")/epv2-evidence}"
DRIVER="$SOURCE/scripts/accept_sp3.sh"

[ -f "$DRIVER" ] || { echo "missing $DRIVER"; exit 1; }
mkdir -p "$EVIDENCE_DIR"
echo "rounds=$ROUNDS tag=$TAG evidence=$EVIDENCE_DIR"

for n in $(seq 1 "$ROUNDS"); do
  log="$EVIDENCE_DIR/acc-$TAG-$n.txt"
  echo "=== round $n start $(date -Is) ==="
  bash "$DRIVER" "$BUILD" "$SOURCE" >"$log" 2>&1
  rc=$?
  rep=$(grep -oE 'evidence: .*' "$log" | tail -1 | cut -d' ' -f2)
  dest="$BUILD/$TAG-round$n"
  if [ -n "$rep" ] && [ -d "$rep" ]; then
    rm -rf "$dest"
    mv "$rep" "$dest"
  fi
  # The log must name the round it came from; reading it back is the only way to
  # know the tag was applied and not merely intended.
  echo "round $n rc=$rc evidence=${rep:-none} log=$log"
  if [ "$rc" != "0" ]; then
    echo "FAILED at round $n; evidence retained in $dest and $log"
    tail -20 "$log"
    exit "$rc"
  fi
done
echo "ALL_ROUNDS_PASS tag=$TAG rounds=$ROUNDS"
