#!/usr/bin/env bash
set -euo pipefail

# EulerPilot v6 pre-candidate readiness check.
# This script is intentionally a development preflight: it may run from a
# dirty closeout worktree and must not be used as a candidate-bound gate.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
cd "$ROOT"

STAMP="$(date +%Y%m%d-%H%M%S)"
OUT_DIR="${EULERPILOT_PREFLIGHT_OUT_DIR:-$ROOT/reports/gates/v6-preflight-$STAMP}"
SMOKE_ROUNDS="${EULERPILOT_GATE_SMOKE_ROUNDS:-1}"
DOCTOR_ROUNDS="${EULERPILOT_GATE_DOCTOR_ROUNDS:-1}"

mkdir -p "$OUT_DIR"

sha256_text() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum | awk '{print $1}'
    else
        shasum -a 256 | awk '{print $1}'
    fi
}

write_meta() {
    {
        printf 'preflight_started_at=%s\n' "$(date -Iseconds)"
        printf 'preflight_head=%s\n' "$(git rev-parse HEAD)"
        printf 'preflight_branch=%s\n' "$(git rev-parse --abbrev-ref HEAD)"
        printf 'preflight_worktree_dirty=%s\n' "$(test -n "$(git status --porcelain)" && echo true || echo false)"
        printf 'preflight_diff_sha256=%s\n' "$(git diff --binary | sha256_text)"
        printf 'preflight_untracked_count=%s\n' "$(git ls-files --others --exclude-standard | wc -l | tr -d ' ')"
    } >"$OUT_DIR/preflight_meta.env"
    git status --short >"$OUT_DIR/git_status_short.txt"
    git diff --stat >"$OUT_DIR/git_diff_stat.txt"
}

run_step() {
    local name="$1"
    shift
    local log="$OUT_DIR/$name.log"
    printf '[preflight] %s\n' "$name"
    if "$@" >"$log" 2>&1; then
        printf '%s=pass\n' "$name" >>"$OUT_DIR/summary.env"
    else
        printf '%s=fail\n' "$name" >>"$OUT_DIR/summary.env"
        cat "$log" >&2
        return 1
    fi
}

docs_stale_phrase_check() {
    ! grep -RInE '24/24|2026-07-25|当前 v6 preflight 24|24 项 P0|121 loopback|新增 benchmark baseline' \
        README.md docs submission reports/v6_preflight_status_20260726.md
}

shell_syntax_check() {
    find scripts tests bench demo -type f -name '*.sh' -print0 |
        xargs -0 -r bash -n
}

python_static_check() {
    python3 -m py_compile \
        scripts/collect_final_evidence.py \
        scripts/collect_scx_stats.py \
        scripts/build_formal_artifact.py \
        scripts/formal_artifact_gate.py \
        scripts/create_release_bundle.py \
        scripts/verify_release_bundle.py
}

web_console_check() {
    if [ ! -d web_console ]; then
        echo "web_console missing" >&2
        return 1
    fi
    (
        cd web_console
        npm --version
        node --version
        if [ ! -x node_modules/.bin/tsc ] || [ ! -d node_modules/yaml ]; then
            npm ci
        fi
        npm test
        npm run lint
        npm run build
    )
}

collect_final_evidence_check() {
    python3 scripts/collect_final_evidence.py \
        --output-md "$OUT_DIR/final_evidence_compact.md" \
        --output-json "$OUT_DIR/final_evidence_compact.json"
}

write_meta
: >"$OUT_DIR/summary.env"

run_step shell_syntax_check shell_syntax_check
run_step python_static_check python_static_check
run_step docs_stale_phrase_check docs_stale_phrase_check
run_step collect_final_evidence collect_final_evidence_check
run_step final_quality_gate env \
    EULERPILOT_GATE_SMOKE_ROUNDS="$SMOKE_ROUNDS" \
    EULERPILOT_GATE_DOCTOR_ROUNDS="$DOCTOR_ROUNDS" \
    bash scripts/final_quality_gate.sh
run_step web_console_check web_console_check

{
    printf 'preflight_finished_at=%s\n' "$(date -Iseconds)"
    printf 'stage_status=PASS_WITH_LIMITATIONS\n'
    printf 'candidate_created=false\n'
    printf 'formal_artifact_created=false\n'
    printf 'formal_experiment_started=false\n'
    printf 'output_dir=%s\n' "$OUT_DIR"
} >>"$OUT_DIR/summary.env"

cat "$OUT_DIR/summary.env"
