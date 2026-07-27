#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
POLICY="$ROOT/agent/src/builtin_skills/policy_engine.cpp"

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

[ -f "$POLICY" ] || fail "missing policy_engine.cpp"

grep -q 'struct TransactionContext' "$POLICY" ||
    fail "PolicyEngine does not define explicit TransactionContext"

grep -q 'std::vector<PolicyEngineAction> actions;' "$POLICY" ||
    fail "TransactionContext does not own per-transaction actions"

grep -q 'bool has_side_effect = false;' "$POLICY" ||
    fail "TransactionContext does not track side effects"

if grep -q 'std::vector<PolicyEngineAction> transaction_actions' "$POLICY"; then
    fail "legacy transaction_actions local vector is still present"
fi

grep -q 'restore_actions(transaction.actions' "$POLICY" ||
    fail "rollback path does not restore per-transaction actions"

grep -q 'active_actions_ = transaction.actions' "$POLICY" ||
    fail "active snapshot is not populated from transaction context"

echo "policy_engine_transaction_model=pass"
