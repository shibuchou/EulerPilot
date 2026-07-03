import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { actionForClient, loadActions } from './actions.js';
import { getAgentDoctor, getAgentSkills, getAgentStatus, getEvents, getEvidenceSummary, getSystem } from './data.js';
import { JobManager } from './jobs.js';
import { resolveConsoleConfig } from './paths.js';

const HERE = path.dirname(fileURLToPath(import.meta.url));

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
      json(res, 202, { job: jobs.start(actionStart[1]) });
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
