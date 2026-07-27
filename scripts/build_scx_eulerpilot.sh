#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [ -z "${KERNEL_SRC:-}" ]; then
    printf '[FAIL] KERNEL_SRC is required; pass the SP4/OLK kernel source path explicitly\n' >&2
    exit 1
fi
SCHED_EXT_DIR="${SCHED_EXT_DIR:-$KERNEL_SRC/tools/sched_ext}"
SCX_BUILD_DIR="${SCX_BUILD_DIR:-$ROOT_DIR/build/scx-tools}"
INSTALL_BIN="${INSTALL_BIN:-/usr/local/bin/scx_eulerpilot}"
LLVM="${LLVM:-1}"

fail() {
    printf '[FAIL] %s\n' "$*" >&2
    exit 1
}

[ -d "$SCHED_EXT_DIR" ] || fail "sched_ext tool directory not found: $SCHED_EXT_DIR"
[ -f "$SCHED_EXT_DIR/Makefile" ] || fail "sched_ext Makefile not found: $SCHED_EXT_DIR/Makefile"

mkdir -p "$SCX_BUILD_DIR"
cp "$ROOT_DIR/sched/scx_eulerpilot.c" "$SCHED_EXT_DIR/scx_eulerpilot.c"
cp "$ROOT_DIR/sched/scx_eulerpilot.bpf.c" "$SCHED_EXT_DIR/scx_eulerpilot.bpf.c"

if ! grep -qw 'scx_eulerpilot' "$SCHED_EXT_DIR/Makefile"; then
    sed -i \
        's/^c-sched-targets = \(.*\)$/c-sched-targets = \1 scx_eulerpilot/' \
        "$SCHED_EXT_DIR/Makefile"
fi

make -C "$SCHED_EXT_DIR" O="$SCX_BUILD_DIR" LLVM="$LLVM" scx_eulerpilot -j"$(nproc)"

install -m 0755 "$SCX_BUILD_DIR/build/bin/scx_eulerpilot" "$INSTALL_BIN"
printf '[INFO] installed %s\n' "$INSTALL_BIN"
"$INSTALL_BIN" --status
