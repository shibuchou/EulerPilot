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

async function waitForJob(manager, jobId, predicate) {
  for (let i = 0; i < 50; i += 1) {
    const job = manager.getJob(jobId);
    if (job && predicate(job)) return job;
    await new Promise((resolve) => setTimeout(resolve, 50));
  }
  throw new Error(`timed out waiting for job ${jobId}`);
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

test('cleanup_action runs after failed mutating job', async () => {
  const rootDir = fs.mkdtempSync(path.join(os.tmpdir(), 'eulerpilot-console-'));
  const jobsDir = path.join(rootDir, 'jobs');
  const cleanupMarker = path.join(rootDir, 'cleanup.marker');
  const fail = makeAction('fail_lab', 'lab', ['node', '-e', 'process.exit(2)']);
  fail.cleanup_action = 'cleanup';
  const cleanup = makeAction('cleanup', 'cleanup', [
    'node',
    '-e',
    "require('node:fs').writeFileSync(process.argv[1], 'cleanup')",
    cleanupMarker
  ]);
  const actions = new Map([
    ['fail_lab', fail],
    ['cleanup', cleanup]
  ]);
  const manager = new JobManager({ rootDir, jobsDir, actions });
  const started = manager.start('fail_lab');
  const job = await waitForJob(manager, started.job_id, (item) => item.process_exited);
  assert.equal(job.status, 'failed');
  assert.equal(job.cleanup_status, 'succeeded');
  assert.equal(fs.readFileSync(cleanupMarker, 'utf8'), 'cleanup');
});

test('cancel status is not overwritten by process exit', async () => {
  const rootDir = fs.mkdtempSync(path.join(os.tmpdir(), 'eulerpilot-console-'));
  const jobsDir = path.join(rootDir, 'jobs');
  const actions = new Map([
    ['slow_lab', makeAction('slow_lab', 'lab', ['node', '-e', 'setTimeout(() => {}, 5000)'])]
  ]);
  const manager = new JobManager({ rootDir, jobsDir, actions });
  const started = manager.start('slow_lab');
  manager.cancel(started.job_id);
  const job = await waitForJob(manager, started.job_id, (item) => item.process_exited);
  assert.equal(job.status, 'canceled');
});
