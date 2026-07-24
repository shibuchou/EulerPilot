import fs from 'node:fs';
import path from 'node:path';
import { EventEmitter } from 'node:events';
import { spawn, spawnSync } from 'node:child_process';
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
      if (job.mutating && !job.process_exited &&
          ['queued', 'running', 'timeout', 'canceled'].includes(job.status)) {
        return true;
      }
      if (job.mutating && job.cleanup_running) return true;
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
      error: job.error || '',
      cleanup_action: job.cleanup_action || '',
      cleanup_status: job.cleanup_status || '',
      cleanup_exit_code: job.cleanup_exit_code ?? null
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
      timeout: null,
      process_exited: false,
      kill_timer: null,
      cleanup_action: action.cleanup_action || '',
      cleanup_running: false,
      cleanup_status: '',
      cleanup_exit_code: null
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
      if (job.kill_timer) clearTimeout(job.kill_timer);
      job.process_exited = true;
      job.ended_at = nowIso();
      job.exit_code = code;
      job.signal = signal || '';
      if (!['timeout', 'canceled'].includes(job.status)) {
        if (job.killed_for_limit) {
          job.status = 'failed';
          job.error = `output exceeded ${action.max_output_bytes} bytes`;
        } else {
          job.status = code === 0 ? 'succeeded' : 'failed';
        }
      }
      if (['failed', 'timeout', 'canceled'].includes(job.status) && action.cleanup_action) {
        this.runCleanupAction(job, action.cleanup_action, logStream);
      }
      logStream.write(`[status] ${job.status} exit_code=${code ?? ''} signal=${signal ?? ''}\n`);
      logStream.end();
      this.emitUpdate(job, 'done', '');
    });
  }

  terminate(job, signal = 'SIGTERM') {
    if (!job || !job.process || job.process_exited) return;
    try {
      if (process.platform !== 'win32' && job.process.pid) {
        process.kill(-job.process.pid, signal);
      } else {
        job.process.kill(signal);
      }
      if (signal === 'SIGTERM' && !job.kill_timer) {
        job.kill_timer = setTimeout(() => {
          if (!job.process_exited) this.terminate(job, 'SIGKILL');
        }, 3000);
      }
    } catch (error) {
      job.error = error.message;
    }
  }

  runCleanupAction(job, cleanupActionId, logStream) {
    const cleanup = this.actions.get(cleanupActionId);
    if (!cleanup) {
      job.cleanup_status = 'missing';
      job.error = `${job.error ? `${job.error}; ` : ''}cleanup action missing: ${cleanupActionId}`;
      return;
    }
    job.cleanup_running = true;
    job.cleanup_action = cleanup.id;
    logStream.write(`[cleanup] start ${cleanup.id}\n`);
    const [cmd, ...args] = cleanup.command;
    const result = spawnSync(cmd, args, {
      cwd: this.rootDir,
      shell: false,
      encoding: 'utf8',
      timeout: cleanup.timeout_seconds * 1000,
      env: {
        ...process.env,
        EULERPILOT_WEB_CONSOLE: '1'
      }
    });
    const cleanupOutput = `${result.stdout || ''}${result.stderr || ''}`;
    if (cleanupOutput) {
      const prefixed = cleanupOutput
        .split(/(?<=\n)/)
        .map((part) => (part.length ? `[cleanup] ${part}` : part))
        .join('');
      logStream.write(prefixed);
      job.log_tail = trimTail(job.log_tail + prefixed);
    }
    job.cleanup_exit_code = result.status ?? null;
    if (result.error || result.status !== 0) {
      job.cleanup_status = 'failed';
      const detail = result.error ? result.error.message : `exit ${result.status}`;
      job.error = `${job.error ? `${job.error}; ` : ''}cleanup failed: ${detail}`;
    } else {
      job.cleanup_status = 'succeeded';
    }
    logStream.write(`[cleanup] ${job.cleanup_status} exit_code=${job.cleanup_exit_code ?? ''}\n`);
    job.cleanup_running = false;
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
    this.terminate(job, 'SIGTERM');
    this.emitUpdate(job, 'status', 'cancel requested\n');
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
