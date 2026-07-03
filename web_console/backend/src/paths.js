import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const DEFAULT_ROOT = path.resolve(HERE, '..', '..', '..');

function samePath(left, right) {
  return path.resolve(left) === path.resolve(right);
}

export function resolveConsoleConfig() {
  const rootDir = path.resolve(process.env.EULERPILOT_CONSOLE_ROOT || DEFAULT_ROOT);
  const cwd = path.resolve(process.cwd());
  if (!samePath(cwd, rootDir)) {
    throw new Error(`Web Console must start from repository root. cwd=${cwd} root=${rootDir}`);
  }
  if (!fs.existsSync(path.join(rootDir, 'AGENTS.md')) || !fs.existsSync(path.join(rootDir, 'web_console'))) {
    throw new Error(`Web Console root does not look like EulerPilot: ${rootDir}`);
  }

  const host = process.env.EULERPILOT_CONSOLE_HOST || '127.0.0.1';
  const port = Number(process.env.EULERPILOT_CONSOLE_PORT || '18080');
  if (!Number.isInteger(port) || port < 1 || port > 65535) {
    throw new Error(`invalid EULERPILOT_CONSOLE_PORT: ${process.env.EULERPILOT_CONSOLE_PORT}`);
  }

  const token = process.env.EULERPILOT_CONSOLE_TOKEN || '';
  const loopback = host === '127.0.0.1' || host === 'localhost' || host === '::1';
  if (!loopback && token.length < 16) {
    throw new Error('non-loopback bind requires EULERPILOT_CONSOLE_TOKEN with at least 16 characters');
  }

  return {
    rootDir,
    host,
    port,
    token,
    requiresToken: !loopback,
    runtimeDir: path.join(rootDir, 'web_console', 'runtime'),
    jobsDir: path.join(rootDir, 'web_console', 'runtime', 'jobs'),
    distDir: path.join(rootDir, 'web_console', 'dist')
  };
}
