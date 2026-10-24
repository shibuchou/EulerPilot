#!/usr/bin/env bash
# EulerPilot v2 -- rebuild the BPF object and regenerate the skeleton the loader
# compiles against. This step used to live only in sched/README.md as prose, which
# made the checked-in scx_schedpilot.bpf.skel.h impossible to reproduce on demand;
# it is a script now, and it prints the hashes that go into PROVENANCE.md.
#
#   build_bpf_sp3.sh [source-dir] [out-dir] [what]
#
# what: "sched" (default, the scheduler object), "events" (the events_trace
# tracing object behind ep-evtmon) or "all".
#
# Needs the SP3 kernel tree (for the sched_ext headers), a vmlinux.h directory and
# bpftool >= 7.3 (a struct_ops skeleton cannot be generated older than that).
# Every path is overridable so the same script works in a fresh environment.
set -euo pipefail

SRC="${1:-/root/epv2-src}"
OUT="${2:-$SRC/sched}"
WHAT="${3:-sched}"
KSRC="${KERNEL_SRC:-/usr/src/linux-6.6.0-145.3.34.165.oe2403sp3.x86_64}"
VMLINUX_DIR="${VMLINUX_DIR:-/root/ep-sched-build/build/include}"
BPFTOOL="${BPFTOOL:-/root/ep-sched-build/build/sbin/bpftool}"
WORK="${WORK:-$(mktemp -d /tmp/epv2-bpf.XXXXXX)}"

[ -f "$SRC/sched/scx_schedpilot.bpf.c" ] || { echo "missing BPF source under $SRC"; exit 1; }
[ -d "$KSRC/tools/sched_ext/include" ] || { echo "missing kernel sched_ext headers: $KSRC"; exit 1; }
[ -f "$VMLINUX_DIR/vmlinux.h" ] || { echo "missing vmlinux.h in $VMLINUX_DIR"; exit 1; }
command -v clang >/dev/null || { echo "missing clang"; exit 1; }
[ -x "$BPFTOOL" ] || { echo "missing bpftool: $BPFTOOL"; exit 1; }
mkdir -p "$WORK" "$OUT"

echo "kernel: $(uname -r)  clang: $(clang --version | head -1 | sed 's/clang version //')"
echo "bpftool: $("$BPFTOOL" --version)"

build_one() { # <source-file> <object-basename> <skeleton-name> <extra-defines...>
  local source="$1" base="$2" name="$3"
  shift 3
  clang -g -O2 -mcpu=v3 -target bpf -D__TARGET_ARCH_x86 "$@" \
    -Wall \
    -I "$SRC/sched" \
    -I "$VMLINUX_DIR" \
    -I "$VMLINUX_DIR/bpf" \
    -I "$KSRC/tools/sched_ext/include" \
    -I "$KSRC/tools/include" \
    -I "$KSRC/tools/include/bpf" \
    -c "$source" -o "$WORK/$base.o"
  "$BPFTOOL" gen skeleton "$WORK/$base.o" name "$name" > "$WORK/$base.skel.h"
  echo "$base object:   $(sha256sum "$WORK/$base.o" | cut -d' ' -f1)"
  echo "$base skeleton: $(sha256sum "$WORK/$base.skel.h" | cut -d' ' -f1)"
  cp "$WORK/$base.o" "$OUT/$base.o"
  cp "$WORK/$base.skel.h" "$OUT/$base.skel.h"
}

case "$WHAT" in
  sched)
    # The upstream scx headers are not warning-clean, so -Wall only covers our
    # own translation unit's own diagnostics; no -Werror (a header warning must
    # not fail a rebuild of a checked-in object).
    build_one "$SRC/sched/scx_schedpilot.bpf.c" scx_schedpilot.bpf scx_schedpilot -D__SCHEDPILOT_BPF__
    ;;
  events)
    build_one "$SRC/sched/events_trace.bpf.c" events_trace.bpf events_trace
    ;;
  all)
    build_one "$SRC/sched/scx_schedpilot.bpf.c" scx_schedpilot.bpf scx_schedpilot -D__SCHEDPILOT_BPF__
    build_one "$SRC/sched/events_trace.bpf.c" events_trace.bpf events_trace
    ;;
  *)
    echo "unknown target: $WHAT (sched|events|all)" >&2
    exit 2
    ;;
esac

echo "installed into $OUT (work kept in $WORK)"
