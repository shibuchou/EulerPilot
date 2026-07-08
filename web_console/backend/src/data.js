import fs from 'node:fs';
import path from 'node:path';
import { spawn } from 'node:child_process';

const GROUP_ORDER = [
  'Agent Framework',
  'CPU Scheduling / PSI',
  'Performance',
  'Network Policy',
  'Security Policy',
  'Resource Control',
  'Policy Engine',
  'Rollback / Cleanup',
  'Quality Gate'
];

function readJson(filePath, fallback = null) {
  try {
    return JSON.parse(fs.readFileSync(filePath, 'utf8'));
  } catch {
    return fallback;
  }
}

function readText(filePath, fallback = '') {
  try {
    return fs.readFileSync(filePath, 'utf8');
  } catch {
    return fallback;
  }
}

function exists(rootDir, relPath) {
  return fs.existsSync(path.join(rootDir, relPath));
}

function runQuick(rootDir, command, timeoutMs = 8000) {
  return new Promise((resolve) => {
    const child = spawn(command[0], command.slice(1), { cwd: rootDir, shell: false });
    let stdout = '';
    let stderr = '';
    const timeout = setTimeout(() => {
      child.kill('SIGTERM');
      resolve({ ok: false, stdout, stderr: `${stderr}\ntimeout` });
    }, timeoutMs);
    child.stdout.on('data', (chunk) => {
      stdout += chunk.toString('utf8');
    });
    child.stderr.on('data', (chunk) => {
      stderr += chunk.toString('utf8');
    });
    child.on('error', (error) => {
      clearTimeout(timeout);
      resolve({ ok: false, stdout, stderr: error.message });
    });
    child.on('close', (code) => {
      clearTimeout(timeout);
      resolve({ ok: code === 0, code, stdout, stderr });
    });
  });
}

function parseOsRelease(text) {
  const out = {};
  for (const line of text.split('\n')) {
    const match = line.match(/^([A-Z_]+)=(.*)$/);
    if (!match) continue;
    out[match[1]] = match[2].replace(/^"|"$/g, '');
  }
  return out;
}

export async function getSystem(rootDir) {
  const [head, status, kernel, hostname] = await Promise.all([
    runQuick(rootDir, ['git', 'rev-parse', '--short', 'HEAD']),
    runQuick(rootDir, ['git', 'status', '--short']),
    runQuick(rootDir, ['uname', '-r']),
    runQuick(rootDir, ['hostname'])
  ]);
  const osRelease = parseOsRelease(readText('/etc/os-release'));
  const mountinfo = readText('/proc/self/mountinfo');
  const cgroupV2 = mountinfo.includes(' - cgroup2 ');
  const psi = fs.existsSync('/proc/pressure/cpu');
  const schedExt = fs.existsSync('/sys/kernel/sched_ext');

  return {
    host: hostname.stdout.trim() || 'unknown',
    root: rootDir,
    git: {
      head: head.stdout.trim() || 'unknown',
      dirty: status.stdout.trim().length > 0,
      status_short: status.stdout.trim().split('\n').filter(Boolean)
    },
    os: {
      pretty_name: osRelease.PRETTY_NAME || process.platform,
      kernel: kernel.stdout.trim() || 'unknown'
    },
    capabilities: {
      cgroup_v2: cgroupV2,
      psi,
      sched_ext: schedExt,
      btf: fs.existsSync('/sys/kernel/btf/vmlinux')
    },
    path_roles: {
      sp3: cgroupV2 ? 'SP3 主交付路径：cgroup v2 已启用' : 'SP3 主交付路径：cgroup v2 不可用',
      scx: schedExt ? 'sched_ext/scx 增强路径：当前内核已可用' : 'sched_ext/scx 增强路径：SP4 / 自编译内核迁移目标'
    },
    files: {
      evidence_manifest: exists(rootDir, 'configs/final_evidence_manifest.json'),
      evidence_compact: exists(rootDir, 'reports/final_evidence_compact.json'),
      dashboard: exists(rootDir, 'reports/dashboard/index.html')
    }
  };
}

export async function getAgentStatus(rootDir) {
  const result = await runQuick(rootDir, ['build/eulerpilot-agent', '--status', '--json'], 10000);
  return {
    ok: result.ok,
    raw: result.stdout || result.stderr,
    json: readJsonFromText(result.stdout)
  };
}

export async function getAgentSkills(rootDir) {
  const result = await runQuick(rootDir, ['build/eulerpilot-agent', '--list-skills'], 10000);
  return {
    ok: result.ok,
    raw: result.stdout || result.stderr,
    skills: result.stdout.split('\n').map((line) => line.trim()).filter(Boolean)
  };
}

export async function getAgentDoctor(rootDir) {
  const result = await runQuick(rootDir, ['build/eulerpilot-agent', '--doctor-skills', '--config', 'configs/agent.yaml'], 30000);
  return {
    ok: result.ok,
    raw: result.stdout || result.stderr
  };
}

function readJsonFromText(text) {
  try {
    return JSON.parse(text);
  } catch {
    return null;
  }
}

function primaryGroup(entry) {
  switch (entry.category) {
    case 'quality_gate':
      return 'Quality Gate';
    case 'repo_status':
      return 'Agent Framework';
    case 'cpu_sched_ext':
      return 'Performance';
    case 'network':
      return 'Network Policy';
    case 'security':
      return 'Security Policy';
    case 'resource_control':
      return 'Resource Control';
    case 'policy_engine':
      return 'Policy Engine';
    default:
      return 'Agent Framework';
  }
}

function hasRollbackEvidence(entry) {
  const files = Array.isArray(entry.evidence_files) ? entry.evidence_files : [];
  const text = `${entry.name || ''} ${entry.notes || ''} ${files.map((item) => item.path).join(' ')}`.toLowerCase();
  return text.includes('rollback') || text.includes('journal') || text.includes('cleanup');
}

export function getEvidenceSummary(rootDir) {
  const compact = readJson(path.join(rootDir, 'reports/final_evidence_compact.json'), {});
  const manifest = readJson(path.join(rootDir, 'configs/final_evidence_manifest.json'), {});
  const entries = Array.isArray(compact.entries) ? compact.entries : [];
  const groups = GROUP_ORDER.map((name) => ({ name, entries: [] }));
  const groupMap = new Map(groups.map((group) => [group.name, group]));

  for (const entry of entries) {
    const group = primaryGroup(entry);
    groupMap.get(group)?.entries.push(entry);
    if (hasRollbackEvidence(entry)) {
      groupMap.get('Rollback / Cleanup')?.entries.push({ ...entry, rollback_highlight: true });
    }
  }

  return {
    generated_at: compact.generated_at || '',
    manifest_title: compact.manifest_title || manifest.title || '',
    total: entries.length,
    required_missing: entries.filter((entry) => entry.required && !entry.exists).length,
    warnings: entries.reduce((count, entry) => count + (Array.isArray(entry.warnings) ? entry.warnings.length : 0), 0),
    status_counts: entries.reduce((acc, entry) => {
      const status = entry.status || 'unknown';
      acc[status] = (acc[status] || 0) + 1;
      return acc;
    }, {}),
    groups,
    entries,
    figures: listFigures(rootDir),
    policy_timeline: readPolicyTimeline(rootDir)
  };
}

function listFigures(rootDir) {
  const dir = path.join(rootDir, 'reports/final_figures');
  try {
    return fs.readdirSync(dir)
      .filter((name) => name.endsWith('.svg'))
      .sort()
      .map((name) => ({ name, url: `/assets/final_figures/${encodeURIComponent(name)}` }));
  } catch {
    return [];
  }
}

function parseJsonl(filePath, limit = 400) {
  const text = readText(filePath);
  if (!text) return [];
  return text
    .split('\n')
    .filter(Boolean)
    .slice(-limit)
    .map((line) => {
      try {
        return JSON.parse(line);
      } catch {
        return { raw: line };
      }
    });
}

function readPolicyTimeline(rootDir) {
  const base = path.join(rootDir, 'results/policy_engine');
  try {
    const dirs = fs.readdirSync(base, { withFileTypes: true })
      .filter((entry) => entry.isDirectory())
      .map((entry) => path.join(base, entry.name))
      .sort()
      .slice(-8);
    const transactions = new Map();
    for (const dir of dirs) {
      for (const file of ['security_policy_events.jsonl', 'policy_engine_events.jsonl', 'resource_control_events.jsonl', 'network_policy_events.jsonl', 'action_journal.jsonl']) {
        const events = parseJsonl(path.join(dir, file), 250);
        for (const event of events) {
          const tx = event.transaction_id || event.trigger_event_id || event.event_id;
          if (!tx) continue;
          if (!transactions.has(tx)) {
            transactions.set(tx, {
              transaction_id: tx,
              source_dir: path.relative(rootDir, dir).replace(/\\/g, '/'),
              events: []
            });
          }
          transactions.get(tx).events.push({
            file,
            skill: event.skill || event.action || file.replace('_events.jsonl', ''),
            stage: event.stage || event.result || event.operation || '',
            policy_id: event.policy_id || '',
            action: event.action || '',
            target: event.target || event.target_ref || '',
            raw: event
          });
        }
      }
    }
    return Array.from(transactions.values()).slice(-8);
  } catch {
    return [];
  }
}

export function getEvents(rootDir, skill, tail = 100) {
  if (!/^[a-z0-9_:-]{1,64}$/i.test(skill)) {
    const error = new Error('invalid skill');
    error.statusCode = 400;
    throw error;
  }
  const safeTail = Math.min(Math.max(Number(tail) || 100, 1), 500);
  const candidates = [
    path.join(rootDir, 'reports/events', `${skill}.jsonl`),
    path.join(rootDir, 'reports/events', `${skill.replace(/:/g, '_')}.jsonl`)
  ];
  const file = candidates.find((candidate) => fs.existsSync(candidate));
  if (!file) {
    return { skill, path: '', events: [] };
  }
  return {
    skill,
    path: path.relative(rootDir, file).replace(/\\/g, '/'),
    events: parseJsonl(file, safeTail)
  };
}
