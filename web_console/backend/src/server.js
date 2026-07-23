import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import { createHash, randomUUID } from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { actionForClient, loadActions } from './actions.js';
import { getAgentDoctor, getAgentSkills, getAgentStatus, getEvents, getEvidenceSummary, getSystem } from './data.js';
import { JobManager } from './jobs.js';
import { resolveConsoleConfig } from './paths.js';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const BODY_LIMIT = 4096;
const CONFIRM_TTL_MS = 60_000;
const confirmationTokens = new Map();

function json(res, statusCode, payload) {
  const body = JSON.stringify(payload);
  res.writeHead(statusCode, {
    'Content-Type': 'application/json; charset=utf-8',
    'Content-Length': Buffer.byteLength(body)
  });
  res.end(body);
}

function text(res, statusCode, body) {
  res.writeHead(statusCode, { 'Content-Type': 'text/plain; charset=utf-8' });
  res.end(body);
}

function notFound(res) {
  json(res, 404, { error: 'not_found' });
}

function isAuthorized(req, config) {
  if (!config.requiresToken) return true;
  const header = req.headers.authorization || '';
  return header === `Bearer ${config.token}`;
}

function stableJson(value) {
  if (value === null || typeof value !== 'object') return JSON.stringify(value);
  if (Array.isArray(value)) return `[${value.map(stableJson).join(',')}]`;
  return `{${Object.keys(value).sort().map((key) => `${JSON.stringify(key)}:${stableJson(value[key])}`).join(',')}}`;
}

function confirmationBody(body) {
  const copy = { ...(body || {}) };
  delete copy.confirm_action_id;
  delete copy.confirm_token;
  return copy;
}

function bodyHash(body) {
  return createHash('sha256').update(stableJson(confirmationBody(body))).digest('hex');
}

function sessionKey(req, config) {
  if (config.requiresToken) {
    return createHash('sha256')
      .update(String(req.headers.authorization || ''))
      .digest('hex');
  }
  return `loopback:${req.socket.remoteAddress || 'local'}`;
}

function pruneConfirmationTokens() {
  const now = Date.now();
  for (const [token, record] of confirmationTokens.entries()) {
    if (record.used || record.expires_at <= now) {
      confirmationTokens.delete(token);
    }
  }
}

function issueConfirmationToken(req, config, actionId, body) {
  pruneConfirmationTokens();
  const token = randomUUID();
  confirmationTokens.set(token, {
    action_id: actionId,
    params_hash: bodyHash(body),
    session_key: sessionKey(req, config),
    expires_at: Date.now() + CONFIRM_TTL_MS,
    used: false
  });
  return token;
}

function consumeConfirmationToken(req, config, actionId, body) {
  pruneConfirmationTokens();
  const token = String(req.headers['x-eulerpilot-confirm-token'] || body.confirm_token || '');
  const record = confirmationTokens.get(token);
  if (!record || record.used) return false;
  if (record.expires_at <= Date.now()) return false;
  if (record.action_id !== actionId) return false;
  if (record.params_hash !== bodyHash(body)) return false;
  if (record.session_key !== sessionKey(req, config)) return false;
  record.used = true;
  confirmationTokens.delete(token);
  return true;
}

function requestHasValidContentType(req) {
  if (req.method !== 'POST' && req.method !== 'PUT' && req.method !== 'PATCH') return true;
  const length = Number(req.headers['content-length'] || '0');
  if (length === 0) return true;
  const contentType = String(req.headers['content-type'] || '').toLowerCase();
  return contentType.startsWith('application/json');
}

function originAllowed(req, config) {
  const origin = req.headers.origin;
  if (!origin) return true;
  try {
    const parsed = new URL(origin);
    const allowed = new Set([
      `http://${config.host}:${config.port}`,
      `http://127.0.0.1:${config.port}`,
      `http://localhost:${config.port}`
    ]);
    return allowed.has(`${parsed.protocol}//${parsed.host}`);
  } catch {
    return false;
  }
}

function fetchMetadataAllowed(req) {
  const site = req.headers['sec-fetch-site'];
  if (!site) return true;
  return site === 'same-origin' || site === 'same-site' || site === 'none';
}

function readJsonBody(req) {
  return new Promise((resolve, reject) => {
    let body = '';
    req.on('data', (chunk) => {
      body += chunk.toString('utf8');
      if (Buffer.byteLength(body, 'utf8') > BODY_LIMIT) {
        const error = new Error('request_body_too_large');
        error.statusCode = 413;
        reject(error);
        req.destroy();
      }
    });
    req.on('end', () => {
      if (!body.trim()) {
        resolve({});
        return;
      }
      try {
        resolve(JSON.parse(body));
      } catch {
        const error = new Error('invalid_json_body');
        error.statusCode = 400;
        reject(error);
      }
    });
    req.on('error', reject);
  });
}

function hasActionConfirmation(req, config, action, body) {
  if (!action.requires_confirm) return true;
  return consumeConfirmationToken(req, config, action.id, body);
}

function serveFile(res, filePath, contentType) {
  fs.readFile(filePath, (error, data) => {
    if (error) {
      notFound(res);
      return;
    }
    res.writeHead(200, { 'Content-Type': contentType });
    res.end(data);
  });
}

function contentTypeFor(filePath) {
  if (filePath.endsWith('.html')) return 'text/html; charset=utf-8';
  if (filePath.endsWith('.js')) return 'text/javascript; charset=utf-8';
  if (filePath.endsWith('.css')) return 'text/css; charset=utf-8';
  if (filePath.endsWith('.svg')) return 'image/svg+xml';
  if (filePath.endsWith('.json')) return 'application/json; charset=utf-8';
  return 'application/octet-stream';
}

function serveStatic(req, res, config, url) {
  if (url.pathname.startsWith('/assets/final_figures/')) {
    const name = decodeURIComponent(url.pathname.replace('/assets/final_figures/', ''));
    if (!/^[a-zA-Z0-9_.-]+\.svg$/.test(name)) {
      notFound(res);
      return;
    }
    serveFile(res, path.join(config.rootDir, 'reports/final_figures', name), 'image/svg+xml');
    return;
  }

  const relative = url.pathname === '/' ? 'index.html' : url.pathname.slice(1);
  if (relative.includes('..')) {
    notFound(res);
    return;
  }
  const candidate = path.join(config.distDir, relative);
  if (candidate.startsWith(config.distDir) && fs.existsSync(candidate) && fs.statSync(candidate).isFile()) {
    serveFile(res, candidate, contentTypeFor(candidate));
    return;
  }
  const indexPath = path.join(config.distDir, 'index.html');
  if (fs.existsSync(indexPath)) {
    serveFile(res, indexPath, 'text/html; charset=utf-8');
    return;
  }
  text(res, 200, 'EulerPilot Web Console API is running. Build frontend with: npm run build\n');
}

async function routeApi(req, res, config, actions, jobs, url) {
  if (!isAuthorized(req, config)) {
    json(res, 401, { error: 'unauthorized' });
    return;
  }
  if (!originAllowed(req, config) || !fetchMetadataAllowed(req)) {
    json(res, 403, { error: 'forbidden_origin' });
    return;
  }
  if (!requestHasValidContentType(req)) {
    json(res, 415, { error: 'unsupported_media_type' });
    return;
  }

  try {
    if (req.method === 'GET' && url.pathname === '/api/health') {
      json(res, 200, { ok: true, service: 'EulerPilot Web Console', time: new Date().toISOString() });
      return;
    }
    if (req.method === 'GET' && url.pathname === '/api/system') {
      json(res, 200, await getSystem(config.rootDir));
      return;
    }
    if (req.method === 'GET' && url.pathname === '/api/actions') {
      json(res, 200, { actions: Array.from(actions.values()).map(actionForClient) });
      return;
    }
    if (req.method === 'GET' && url.pathname === '/api/jobs') {
      json(res, 200, { jobs: jobs.listJobs() });
      return;
    }
    if (req.method === 'GET' && url.pathname === '/api/agent/status') {
      json(res, 200, await getAgentStatus(config.rootDir));
      return;
    }
    if (req.method === 'GET' && url.pathname === '/api/agent/skills') {
      json(res, 200, await getAgentSkills(config.rootDir));
      return;
    }
    if (req.method === 'GET' && url.pathname === '/api/agent/doctor') {
      json(res, 200, await getAgentDoctor(config.rootDir));
      return;
    }
    if (req.method === 'GET' && url.pathname === '/api/evidence/summary') {
      json(res, 200, getEvidenceSummary(config.rootDir));
      return;
    }
    if (req.method === 'GET' && url.pathname === '/api/events') {
      json(res, 200, getEvents(config.rootDir, url.searchParams.get('skill') || '', url.searchParams.get('tail') || '100'));
      return;
    }

    const actionStart = url.pathname.match(/^\/api\/actions\/([a-z0-9_]+)\/start$/);
    if (req.method === 'POST' && actionStart) {
      const action = actions.get(actionStart[1]);
      if (!action) {
        notFound(res);
        return;
      }
      const body = await readJsonBody(req);
      if (!hasActionConfirmation(req, config, action, body)) {
        const token = issueConfirmationToken(req, config, action.id, body);
        json(res, 428, {
          error: 'confirmation_required',
          action_id: action.id,
          confirmation_token: token,
          expires_in_ms: CONFIRM_TTL_MS,
          safe_description: action.safe_description,
          risk_description: action.risk_description
        });
        return;
      }
      json(res, 202, { job: jobs.start(action.id) });
      return;
    }

    const jobMatch = url.pathname.match(/^\/api\/jobs\/([a-z0-9_-]+)$/);
    if (req.method === 'GET' && jobMatch) {
      const job = jobs.getJob(jobMatch[1]);
      if (!job) {
        notFound(res);
        return;
      }
      json(res, 200, { job: jobs.serializeJob(job) });
      return;
    }
    if (req.method === 'POST' && jobMatch && url.pathname.endsWith('/cancel')) {
      notFound(res);
      return;
    }

    const cancelMatch = url.pathname.match(/^\/api\/jobs\/([a-z0-9_-]+)\/cancel$/);
    if (req.method === 'POST' && cancelMatch) {
      const body = await readJsonBody(req);
      if (!consumeConfirmationToken(req, config, `cancel:${cancelMatch[1]}`, body)) {
        const token = issueConfirmationToken(req, config, `cancel:${cancelMatch[1]}`, body);
        json(res, 428, {
          error: 'confirmation_required',
          action_id: `cancel:${cancelMatch[1]}`,
          confirmation_token: token,
          expires_in_ms: CONFIRM_TTL_MS,
          safe_description: '取消正在运行的 Web Console job。',
          risk_description: '会向该 job 的进程组发送终止信号。'
        });
        return;
      }
      json(res, 200, { job: jobs.cancel(cancelMatch[1]) });
      return;
    }

    const streamMatch = url.pathname.match(/^\/api\/jobs\/([a-z0-9_-]+)\/stream$/);
    if (req.method === 'GET' && streamMatch) {
      streamJob(res, jobs, streamMatch[1]);
      return;
    }

    notFound(res);
  } catch (error) {
    json(res, error.statusCode || 500, { error: error.message || 'internal_error' });
  }
}

function streamJob(res, jobs, jobId) {
  const job = jobs.getJob(jobId);
  if (!job) {
    notFound(res);
    return;
  }
  res.writeHead(200, {
    'Content-Type': 'text/event-stream; charset=utf-8',
    'Cache-Control': 'no-cache',
    Connection: 'keep-alive'
  });
  const send = (event, payload) => {
    res.write(`event: ${event}\n`);
    res.write(`data: ${JSON.stringify(payload)}\n\n`);
  };
  send('snapshot', { job: jobs.serializeJob(job), tail: job.log_tail });
  if (!['queued', 'running'].includes(job.status)) {
    send('done', { job: jobs.serializeJob(job), chunk: '' });
    res.end();
    return;
  }
  const listener = (event) => {
    if (event.job_id !== jobId) return;
    send(event.type, event);
    if (event.type === 'done') {
      res.end();
      jobs.off('job-event', listener);
    }
  };
  jobs.on('job-event', listener);
  res.on('close', () => jobs.off('job-event', listener));
}

export function createConsoleServer() {
  const config = resolveConsoleConfig();
  const actions = loadActions(config.rootDir);
  const jobs = new JobManager({ rootDir: config.rootDir, jobsDir: config.jobsDir, actions });

  const server = http.createServer(async (req, res) => {
    const url = new URL(req.url || '/', `http://${req.headers.host || '127.0.0.1'}`);
    if (url.pathname.startsWith('/api/')) {
      await routeApi(req, res, config, actions, jobs, url);
      return;
    }
    serveStatic(req, res, config, url);
  });

  return { server, config, actions, jobs };
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const { server, config } = createConsoleServer();
  server.listen(config.port, config.host, () => {
    console.log(`EulerPilot Web Console listening on http://${config.host}:${config.port}`);
    console.log(`root=${config.rootDir}`);
  });
}
