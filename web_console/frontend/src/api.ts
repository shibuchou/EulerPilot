export type ActionKind = 'readonly' | 'verify' | 'demo' | 'lab' | 'cleanup';

export interface ConsoleAction {
  id: string;
  title: string;
  kind: ActionKind;
  command: string[];
  timeout_seconds: number;
  max_output_bytes: number;
  requires_confirm: boolean;
  cleanup_action: string;
  parse_json: boolean;
  safe_description: string;
  risk_description: string;
}

export interface Job {
  job_id: string;
  action_id: string;
  kind: ActionKind;
  status: 'queued' | 'running' | 'succeeded' | 'failed' | 'canceled' | 'timeout';
  started_at: string;
  ended_at: string;
  exit_code: number | null;
  signal: string;
  log_tail: string;
  log_file: string;
  command: string[];
  pid: number | null;
  error: string;
}

export interface SystemInfo {
  host: string;
  root: string;
  git: {
    head: string;
    dirty: boolean;
    status_short: string[];
  };
  os: {
    pretty_name: string;
    kernel: string;
  };
  capabilities: Record<string, boolean>;
  path_roles: {
    sp3: string;
    scx: string;
  };
  files: Record<string, boolean>;
}

export interface EvidenceEntry {
  category: string;
  name: string;
  host: string;
  path: string;
  kind: string;
  required: boolean;
  exists: boolean;
  status: string;
  notes: string;
  summary?: Record<string, unknown>;
  evidence_files?: Array<{ path: string; exists: boolean; bytes?: number }>;
  warnings?: string[];
  rollback_highlight?: boolean;
}

export interface EvidenceGroup {
  name: string;
  entries: EvidenceEntry[];
}

export interface FigureItem {
  name: string;
  url: string;
}

export interface TimelineEvent {
  file: string;
  skill: string;
  stage: string;
  policy_id: string;
  action: string;
  target: unknown;
  raw: Record<string, unknown>;
}

export interface PolicyTransaction {
  transaction_id: string;
  source_dir: string;
  events: TimelineEvent[];
}

export interface EvidenceSummary {
  generated_at: string;
  manifest_title: string;
  total: number;
  required_missing: number;
  warnings: number;
  status_counts: Record<string, number>;
  groups: EvidenceGroup[];
  entries: EvidenceEntry[];
  figures: FigureItem[];
  policy_timeline: PolicyTransaction[];
}

export interface AgentStatus {
  ok: boolean;
  raw: string;
  json: { skills?: Array<Record<string, unknown>> } | null;
}

export interface AgentSkills {
  ok: boolean;
  raw: string;
  skills: string[];
}

async function getJson<T>(path: string): Promise<T> {
  const response = await fetch(path);
  if (!response.ok) {
    throw new Error(`${path} -> ${response.status}`);
  }
  return response.json() as Promise<T>;
}

async function postJson<T>(path: string, body?: Record<string, unknown>, headers?: Record<string, string>): Promise<T> {
  const response = await fetch(path, {
    method: 'POST',
    headers: {
      ...(body ? { 'Content-Type': 'application/json' } : {}),
      ...(headers || {})
    },
    body: body ? JSON.stringify(body) : undefined
  });
  if (!response.ok) {
    const payload = await response.json().catch(() => ({ error: response.statusText }));
    const error = new Error(String(payload.error || response.statusText)) as Error & {
      payload?: Record<string, unknown>;
      statusCode?: number;
    };
    error.payload = payload;
    error.statusCode = response.status;
    throw error;
  }
  return response.json() as Promise<T>;
}

async function postWithConfirmation<T>(path: string): Promise<T> {
  try {
    return await postJson<T>(path);
  } catch (error) {
    const typed = error as Error & {
      payload?: Record<string, unknown>;
      statusCode?: number;
    };
    const token = typeof typed.payload?.confirmation_token === 'string'
      ? typed.payload.confirmation_token
      : '';
    if (typed.statusCode !== 428 || !token) {
      throw error;
    }
    return postJson<T>(
      path,
      { confirm_token: token },
      { 'X-EulerPilot-Confirm-Token': token }
    );
  }
}

export const api = {
  health: () => getJson<{ ok: boolean; time: string }>('/api/health'),
  system: () => getJson<SystemInfo>('/api/system'),
  actions: () => getJson<{ actions: ConsoleAction[] }>('/api/actions'),
  jobs: () => getJson<{ jobs: Job[] }>('/api/jobs'),
  status: () => getJson<AgentStatus>('/api/agent/status'),
  skills: () => getJson<AgentSkills>('/api/agent/skills'),
  doctor: () => getJson<{ ok: boolean; raw: string }>('/api/agent/doctor'),
  evidence: () => getJson<EvidenceSummary>('/api/evidence/summary'),
  events: (skill: string, tail = 100) => getJson<{ skill: string; path: string; events: Record<string, unknown>[] }>(`/api/events?skill=${encodeURIComponent(skill)}&tail=${tail}`),
  startAction: (id: string, confirmed = false) => confirmed
    ? postWithConfirmation<{ job: Job }>(`/api/actions/${id}/start`)
    : postJson<{ job: Job }>(`/api/actions/${id}/start`),
  cancelJob: (id: string) => postWithConfirmation<{ job: Job }>(`/api/jobs/${id}/cancel`)
};
