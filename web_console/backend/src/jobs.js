import fs from 'node:fs';
import path from 'node:path';
import { EventEmitter } from 'node:events';
import { spawn } from 'node:child_process';
import { randomUUID } from 'node:crypto';
import { isMutatingAction } from './actions.js';

const TAIL_LIMIT = 1024 * 1024;
const RECENT_LIMIT = 50;

function nowIso() {
  return new Date().toISOString();
}

function trimTail(value, limit = TAIL_LIMIT) {
  if (Buffer.byteLength(value, 'utf8') <= limit) return value;
  return value.slice(-limit);
}

export class JobManager extends EventEmitter {
  constructor({ rootDir, jobsDir, actions }) {
    super();
    this.rootDir = rootDir;
    this.jobsDir = jobsDir;
    this.actions = actions;
    this.jobs = new Map();
    fs.mkdirSync(jobsDir, { recursive: true });
  }

  hasRunningMutatingJob() {
    for (const job of this.jobs.values()) {
      if ((job.status === 'queued' || job.status === 'running') && job.mutating) {
        return true;
      }
    }
    return false;
  }

  listJobs() {
    return Array.from(this.jobs.values())
      .sort((a, b) => String(b.started_at || '').localeCompare(String(a.started_at || '')))
      .slice(0, RECENT_LIMIT)
      .map((job) => this.serializeJob(job));
  }

  getJob(id) {
    return this.jobs.get(id);
  }

  serializeJob(job) {
    return {
      job_id: job.job_id,
      action_id: job.action_id,
      kind: job.kind,
      status: job.status,
      started_at: job.started_at,
      ended_at: job.ended_at,
      exit_code: job.exit_code,
      signal: job.signal,
      log_tail: job.log_tail,
      log_file: path.relative(this.rootDir, job.log_file).replace(/\\/g, '/'),
      command: job.command,
      pid: job.pid || null,
      error: job.error || ''
    };
  }

  start(actionId) {
    const action = this.actions.get(actionId);
    if (!action) {
      const error = new Error(`unknown action: ${actionId}`);
      error.statusCode = 404;
      throw error;
    }
    const mutating = isMutatingAction(action);
    if (mutating && this.hasRunningMutatingJob()) {
      const error = new Error('another demo/lab/cleanup job is already running');
      error.statusCode = 423;
      throw error;
    }

    const jobId = `${actionId}-${Date.now()}-${randomUUID().slice(0, 8)}`;
    const logFile = path.join(this.jobsDir, `${jobId}.log`);
    const job = {
      job_id: jobId,
      action_id: action.id,
      kind: action.kind,
      command: action.command,
      mutating,
      status: 'queued',
      started_at: nowIso(),
      ended_at: '',
      exit_code: null,
      signal: '',
      log_tail: '',
      log_file: logFile,
      pid: null,
      error: '',
      output_bytes: 0,
      process: null,
      killed_for_limit: false,
      timeout: null
    };
    this.jobs.set(jobId, job);
    this.pruneJobs();
    this.spawnJob(job, action);
    return this.serializeJob(job);
  }

  spawnJob(job, action) {
    const [cmd, ...args] = action.command;
    const logStream = fs.createWriteStream(job.log_file, { flags: 'a' });
    const child = spawn(cmd, args, {
      cwd: this.rootDir,
      shell: false,
      detached: process.platform !== 'win32',
      env: {
        ...process.env,
        EULERPILOT_WEB_CONSOLE: '1'
      }
    });

    job.process = child;
    job.pid = child.pid;
    job.status = 'running';
    this.emitUpdate(job, 'status', `started ${action.title}\n`);

    const append = (streamName, chunk) => {
      const text = chunk.toString('utf8');
      const prefixed = text
        .split(/(?<=\n)/)
        .map((part) => (part.length ? `[${streamName}] ${part}` : part))
        .join('');
      job.output_bytes += Buffer.byteLength(prefixed, 'utf8');
      logStream.write(prefixed);
      job.log_tail = trimTail(job.log_tail + prefixed);
      this.emitUpdate(job, 'output', prefixed);
      if (job.output_bytes > action.max_output_bytes && !job.killed_for_limit) {
        job.killed_for_limit = true;
        this.terminate(job, 'SIGTERM');
      }
    };

    child.stdout.on('data', (chunk) => append('stdout', chunk));
    child.stderr.on('data', (chunk) => append('stderr', chunk));

    child.on('error', (error) => {
      job.error = error.message;
      append('error', `${error.message}\n`);
    });

    job.timeout = setTimeout(() => {
      job.status = 'timeout';
      job.error = `timeout after ${action.timeout_seconds}s`;
      this.terminate(job, 'SIGTERM');
    }, action.timeout_seconds * 1000);

    child.on('close', (code, signal) => {
      clearTimeout(job.timeout);
      job.ended_at = nowIso();
      job.exit_code = code;
      job.signal = signal || '';
      if (job.status !== 'timeout') {
        if (job.killed_for_limit) {
          job.status = 'failed';
          job.error = `output exceeded ${action.max_output_bytes} bytes`;
        } else {
          job.status = code === 0 ? 'succeeded' : 'failed';
        }
      }
      logStream.write(`[status] ${job.status} exit_code=${code ?? ''} signal=${signal ?? ''}\n`);
      logStream.end();
      this.emitUpdate(job, 'done', '');
    });
  }

  terminate(job, signal = 'SIGTERM') {
    if (!job || !job.process || job.process.killed) return;
    try {
      if (process.platform !== 'win32' && job.process.pid) {
        process.kill(-job.process.pid, signal);
      } else {
        job.process.kill(signal);
      }
    } catch (error) {
      job.error = error.message;
    }
  }

  cancel(jobId) {
    const job = this.jobs.get(jobId);
    if (!job) {
      const error = new Error(`unknown job: ${jobId}`);
      error.statusCode = 404;
      throw error;
    }
    if (job.status !== 'queued' && job.status !== 'running') {
      return this.serializeJob(job);
    }
    job.status = 'canceled';
    job.ended_at = nowIso();
    this.terminate(job, 'SIGTERM');
    this.emitUpdate(job, 'done', 'canceled by user\n');
    return this.serializeJob(job);
  }

  emitUpdate(job, type, chunk) {
    this.emit('job-event', {
      job_id: job.job_id,
      type,
      chunk,
      job: this.serializeJob(job)
    });
  }

  pruneJobs() {
    const jobs = Array.from(this.jobs.values()).sort((a, b) => String(b.started_at).localeCompare(String(a.started_at)));
    for (const job of jobs.slice(RECENT_LIMIT)) {
      if (job.status !== 'queued' && job.status !== 'running') {
        this.jobs.delete(job.job_id);
      }
    }
  }
}
