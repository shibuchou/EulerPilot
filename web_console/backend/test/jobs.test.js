import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { JobManager } from '../src/jobs.js';

function makeAction(id, kind, command) {
  return {
    id,
    title: id,
    kind,
    command,
    timeout_seconds: 5,
    max_output_bytes: 1024 * 1024,
    requires_confirm: false,
    cleanup_action: '',
    parse_json: false,
    safe_description: '',
    risk_description: ''
  };
}

test('demo/lab/cleanup actions share a single mutating job slot', async () => {
  const rootDir = fs.mkdtempSync(path.join(os.tmpdir(), 'eulerpilot-console-'));
  const jobsDir = path.join(rootDir, 'jobs');
  const actions = new Map([
    ['slow_demo', makeAction('slow_demo', 'demo', ['node', '-e', 'setTimeout(() => {}, 1500)'])],
    ['cleanup', makeAction('cleanup', 'cleanup', ['node', '-e', 'process.exit(0)'])]
  ]);
  const manager = new JobManager({ rootDir, jobsDir, actions });
  const first = manager.start('slow_demo');
  assert.equal(first.status, 'running');
  assert.throws(() => manager.start('cleanup'), (error) => {
    assert.equal(error.statusCode, 423);
    return true;
  });
  manager.cancel(first.job_id);
  await new Promise((resolve) => setTimeout(resolve, 100));
});

test('readonly actions can run while a demo job is active', async () => {
  const rootDir = fs.mkdtempSync(path.join(os.tmpdir(), 'eulerpilot-console-'));
  const jobsDir = path.join(rootDir, 'jobs');
  const actions = new Map([
    ['slow_demo', makeAction('slow_demo', 'demo', ['node', '-e', 'setTimeout(() => {}, 1000)'])],
    ['readonly', makeAction('readonly', 'readonly', ['node', '-e', 'process.exit(0)'])]
  ]);
  const manager = new JobManager({ rootDir, jobsDir, actions });
  const first = manager.start('slow_demo');
  const second = manager.start('readonly');
  assert.equal(second.status, 'running');
  manager.cancel(first.job_id);
  await new Promise((resolve) => setTimeout(resolve, 100));
});
