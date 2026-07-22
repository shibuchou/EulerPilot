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
