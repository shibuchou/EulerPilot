import test from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { loadActions, normalizeAction } from '../src/actions.js';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, '..', '..', '..');

test('loads frozen action whitelist', () => {
  const actions = loadActions(ROOT);
  assert.equal(actions.has('demo_offline'), true);
  assert.equal(actions.has('policy_engine_lab'), true);
  assert.equal(actions.get('demo_offline').kind, 'demo');
  assert.equal(actions.get('demo_offline').requires_confirm, false);
  assert.match(actions.get('demo_offline').safe_description, /离线演示/);
});

test('rejects path traversal command items', () => {
  assert.throws(() => normalizeAction('bad_action', {
    kind: 'readonly',
    command: ['bash', '../scripts/check_env.sh'],
    timeout_seconds: 10,
    max_output_bytes: 4096
  }), /must not contain/);
});

test('rejects absolute command paths', () => {
  assert.throws(() => normalizeAction('bad_action', {
    kind: 'readonly',
    command: ['/bin/bash', 'scripts/check_env.sh'],
    timeout_seconds: 10,
    max_output_bytes: 4096
  }), /absolute path/);
});

test('rejects unknown action kind', () => {
  assert.throws(() => normalizeAction('bad_action', {
    kind: 'danger',
    command: ['bash', 'scripts/check_env.sh'],
    timeout_seconds: 10,
    max_output_bytes: 4096
  }), /invalid kind/);
});
