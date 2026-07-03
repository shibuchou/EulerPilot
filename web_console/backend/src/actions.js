import fs from 'node:fs';
import path from 'node:path';
import YAML from 'yaml';

export const ACTION_KINDS = new Set(['readonly', 'verify', 'demo', 'lab', 'cleanup']);
export const MUTATING_KINDS = new Set(['demo', 'lab', 'cleanup']);

const ID_PATTERN = /^[a-z][a-z0-9_]{1,63}$/;

function hasUnsafePathSegment(value) {
  return value.split(/[\\/]+/).some((part) => part === '..');
}

function validateCommandItem(item, field) {
  if (typeof item !== 'string' || item.length === 0) {
    throw new Error(`${field} must be a non-empty string`);
  }
  if (item.includes('\0')) {
    throw new Error(`${field} contains NUL byte`);
  }
  if (path.isAbsolute(item)) {
    throw new Error(`${field} must not be an absolute path: ${item}`);
  }
  if (hasUnsafePathSegment(item)) {
    throw new Error(`${field} must not contain '..': ${item}`);
  }
}

export function normalizeAction(id, raw) {
  if (!ID_PATTERN.test(id)) {
    throw new Error(`invalid action id: ${id}`);
  }
  if (!raw || typeof raw !== 'object') {
    throw new Error(`action ${id} must be an object`);
  }
  if (raw.id && raw.id !== id) {
    throw new Error(`action ${id} has mismatched id field: ${raw.id}`);
  }

  const kind = String(raw.kind || '');
  if (!ACTION_KINDS.has(kind)) {
    throw new Error(`action ${id} has invalid kind: ${kind}`);
  }
  if (!Array.isArray(raw.command) || raw.command.length === 0) {
    throw new Error(`action ${id} command must be a non-empty string array`);
  }
  raw.command.forEach((item, index) => validateCommandItem(item, `action ${id} command[${index}]`));

  const timeoutSeconds = Number(raw.timeout_seconds);
  if (!Number.isInteger(timeoutSeconds) || timeoutSeconds < 1 || timeoutSeconds > 3600) {
    throw new Error(`action ${id} timeout_seconds must be 1..3600`);
  }
  const maxOutputBytes = Number(raw.max_output_bytes ?? 1048576);
  if (!Number.isInteger(maxOutputBytes) || maxOutputBytes < 1024 || maxOutputBytes > 32 * 1024 * 1024) {
    throw new Error(`action ${id} max_output_bytes must be 1KB..32MB`);
  }

  return {
    id,
    title: String(raw.title || id),
    kind,
    command: raw.command.map(String),
    timeout_seconds: timeoutSeconds,
    max_output_bytes: maxOutputBytes,
    requires_confirm: Boolean(raw.requires_confirm),
    cleanup_action: raw.cleanup_action ? String(raw.cleanup_action) : '',
    parse_json: Boolean(raw.parse_json),
    safe_description: String(raw.safe_description || ''),
    risk_description: String(raw.risk_description || '')
  };
}

export function loadActions(rootDir) {
  const configPath = path.join(rootDir, 'web_console', 'config', 'actions.yaml');
  const text = fs.readFileSync(configPath, 'utf8');
  const doc = YAML.parse(text);
  if (!doc || typeof doc !== 'object' || !doc.actions || typeof doc.actions !== 'object') {
    throw new Error('actions.yaml must contain an actions object');
  }

  const actions = new Map();
  for (const [id, raw] of Object.entries(doc.actions)) {
    const action = normalizeAction(id, raw);
    actions.set(id, action);
  }
  for (const action of actions.values()) {
    if (action.cleanup_action && !actions.has(action.cleanup_action)) {
      throw new Error(`action ${action.id} references missing cleanup_action ${action.cleanup_action}`);
    }
  }
  return actions;
}

export function actionForClient(action) {
  return {
    id: action.id,
    title: action.title,
    kind: action.kind,
    command: action.command,
    timeout_seconds: action.timeout_seconds,
    max_output_bytes: action.max_output_bytes,
    requires_confirm: action.requires_confirm,
    cleanup_action: action.cleanup_action,
    parse_json: action.parse_json,
    safe_description: action.safe_description,
    risk_description: action.risk_description
  };
}

export function isMutatingAction(action) {
  return MUTATING_KINDS.has(action.kind);
}
