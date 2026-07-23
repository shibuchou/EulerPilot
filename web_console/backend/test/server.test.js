import test from 'node:test';
import assert from 'node:assert/strict';
import http from 'node:http';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createConsoleServer } from '../src/server.js';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, '..', '..', '..');

function requestJson(server, method, requestPath, body, headers = {}) {
  const address = server.address();
  return new Promise((resolve, reject) => {
    const payload = body ? JSON.stringify(body) : '';
    const req = http.request({
      host: '127.0.0.1',
      port: address.port,
      path: requestPath,
      method,
      headers: {
        ...(payload ? { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(payload) } : {}),
        ...headers
      }
    }, (res) => {
      let data = '';
      res.on('data', (chunk) => {
        data += chunk.toString('utf8');
      });
      res.on('end', () => {
        resolve({ statusCode: res.statusCode, body: data ? JSON.parse(data) : {} });
      });
    });
    req.on('error', reject);
    if (payload) req.write(payload);
    req.end();
  });
}

function requestRaw(server, method, requestPath, payload, headers = {}) {
  const address = server.address();
  return new Promise((resolve, reject) => {
    const req = http.request({
      host: '127.0.0.1',
      port: address.port,
      path: requestPath,
      method,
      headers: {
        'Content-Length': Buffer.byteLength(payload),
        ...headers
      }
    }, (res) => {
      let data = '';
      res.on('data', (chunk) => {
        data += chunk.toString('utf8');
      });
      res.on('end', () => {
        resolve({ statusCode: res.statusCode, body: data ? JSON.parse(data) : {} });
      });
    });
    req.on('error', reject);
    req.write(payload);
    req.end();
  });
}

test('confirmed actions require a backend confirmation token', async () => {
  const oldCwd = process.cwd();
  process.chdir(ROOT);
  const { server } = createConsoleServer();
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  try {
    const response = await requestJson(server, 'POST', '/api/actions/policy_engine_lab/start');
    assert.equal(response.statusCode, 428);
    assert.equal(response.body.error, 'confirmation_required');
    assert.equal(response.body.action_id, 'policy_engine_lab');
  } finally {
    await new Promise((resolve) => server.close(resolve));
    process.chdir(oldCwd);
  }
});

test('confirmation tokens are one-shot and bound to cancel action', async () => {
  const oldCwd = process.cwd();
  process.chdir(ROOT);
  const { server } = createConsoleServer();
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  try {
    const first = await requestJson(server, 'POST', '/api/jobs/nonexistent/cancel');
    assert.equal(first.statusCode, 428);
    assert.equal(first.body.error, 'confirmation_required');
    assert.equal(first.body.action_id, 'cancel:nonexistent');
    assert.equal(typeof first.body.confirmation_token, 'string');

    const token = first.body.confirmation_token;
    const accepted = await requestJson(
      server,
      'POST',
      '/api/jobs/nonexistent/cancel',
      { confirm_token: token },
      { 'X-EulerPilot-Confirm-Token': token }
    );
    assert.equal(accepted.statusCode, 404);
    assert.equal(accepted.body.error, 'unknown job: nonexistent');

    const reused = await requestJson(
      server,
      'POST',
      '/api/jobs/nonexistent/cancel',
      { confirm_token: token },
      { 'X-EulerPilot-Confirm-Token': token }
    );
    assert.equal(reused.statusCode, 428);
    assert.equal(reused.body.error, 'confirmation_required');
  } finally {
    await new Promise((resolve) => server.close(resolve));
    process.chdir(oldCwd);
  }
});

test('POST requests with bodies must use JSON content type', async () => {
  const oldCwd = process.cwd();
  process.chdir(ROOT);
  const { server } = createConsoleServer();
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  try {
    const response = await requestRaw(
      server,
      'POST',
      '/api/jobs/nonexistent/cancel',
      '{}',
      { 'Content-Type': 'text/plain' }
    );
    assert.equal(response.statusCode, 415);
    assert.equal(response.body.error, 'unsupported_media_type');
  } finally {
    await new Promise((resolve) => server.close(resolve));
    process.chdir(oldCwd);
  }
});
