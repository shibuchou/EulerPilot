import {
  Activity,
  BadgeCheck,
  BarChart3,
  Braces,
  ClipboardCheck,
  Database,
  FileCode2,
  FileText,
  FlaskConical,
  Gauge,
  GitBranch,
  Layers3,
  ListChecks,
  Network,
  Play,
  RefreshCw,
  ServerCog,
  Shield,
  Shell,
  Square,
  SquareTerminal,
  Trash2,
  Wrench,
  type LucideIcon
} from 'lucide-react';
import { useEffect, useMemo, useState } from 'react';
import { AgentSkills, AgentStatus, api, ConsoleAction, EvidenceEntry, EvidenceSummary, Job, PolicyTransaction, SystemInfo } from './api';

type PageKey = 'overview' | 'skills' | 'scheduling' | 'extensions' | 'policy' | 'evidence';
type ExtensionTab = 'network' | 'security' | 'resource';

const pages: Array<{ key: PageKey; label: string; short: string; icon: LucideIcon }> = [
  { key: 'overview', label: '总览', short: '总览', icon: Gauge },
  { key: 'skills', label: 'Skills 与 Agent', short: 'Skills', icon: Layers3 },
  { key: 'scheduling', label: '调度与 PSI', short: '调度', icon: BarChart3 },
  { key: 'extensions', label: 'eBPF 扩展能力', short: 'eBPF', icon: Network },
  { key: 'policy', label: 'Policy Engine 时间线', short: '联动', icon: GitBranch },
  { key: 'evidence', label: '证据与现场演示', short: '证据', icon: ClipboardCheck }
];

const recommendedActions = ['check_env', 'list_skills', 'status_json', 'doctor_skills', 'demo_offline', 'policy_engine_lab', 'demo_cleanup'];
const advancedActions = ['policy_engine_real_pod', 'final_quality_gate'];

const actionTitleZh: Record<string, string> = {
  check_env: '环境检查',
  list_skills: '查看 Skills',
  status_json: 'Agent 状态 JSON',
  validate_default_config: '校验默认配置',
  validate_policy_engine_config: '校验联动配置',
  doctor_skills: 'Skill 诊断',
  demo_offline: '离线证据演示',
  policy_engine_lab: '跨 Skill 联动实验',
  policy_engine_real_pod: '真实 Pod 联动',
  demo_cleanup: '清理现场资源',
  final_quality_gate: '最终质量门禁'
};

const kindLabelZh: Record<ConsoleAction['kind'], string> = {
  readonly: '只读',
  verify: '校验',
  demo: '演示',
  lab: '实验',
  cleanup: '清理'
};

const jobStatusZh: Record<Job['status'], string> = {
  queued: '排队中',
  running: '运行中',
  succeeded: '成功',
  failed: '失败',
  canceled: '已取消',
  timeout: '超时'
};

const groupLabelZh: Record<string, string> = {
  'Agent Framework': 'Agent 框架',
  'CPU Scheduling / PSI': 'CPU 调度 / PSI',
  Performance: '性能结果',
  'Network Policy': '网络策略',
  'Security Policy': '安全策略',
  'Resource Control': '资源管控',
  'Policy Engine': '策略引擎',
  'Rollback / Cleanup': '回滚 / 清理',
  'Quality Gate': '质量门禁'
};

const capabilityLabelZh: Record<string, string> = {
  cgroup_v2: 'cgroup v2',
  psi: 'PSI',
  sched_ext: 'sched_ext',
  btf: 'BTF'
};

function isMutating(action: ConsoleAction) {
  return action.kind === 'demo' || action.kind === 'lab' || action.kind === 'cleanup';
}

function statusTone(value: string | boolean | undefined) {
  if (value === true || value === 'pass' || value === 'present' || value === 'succeeded') return 'ok';
  if (value === false || value === 'failed' || value === 'timeout') return 'bad';
  return 'warn';
}

function boolLabel(value: unknown) {
  return value === true ? '是' : value === false ? '否' : formatUnknown(value);
}

function actionTitle(action: ConsoleAction) {
  return actionTitleZh[action.id] || action.title;
}

function commandKind(action: ConsoleAction) {
  const command = action.command.join(' ');
  if (command.includes('tests/integration/')) return '集成测试';
  if (command.endsWith('.sh') || command.includes('.sh ')) return 'Shell 脚本';
  if (command.includes('eulerpilot-agent')) return 'Agent CLI';
  return '命令';
}

function actionIcon(action: ConsoleAction): LucideIcon {
  if (action.kind === 'cleanup') return Trash2;
  if (action.id === 'check_env') return ServerCog;
  if (action.id.startsWith('validate_')) return Wrench;
  if (action.id === 'doctor_skills' || action.id === 'final_quality_gate') return FlaskConical;
  if (action.command.join(' ').includes('tests/integration/')) return FileCode2;
  if (action.command.join(' ').includes('.sh')) return Shell;
  if (action.id.includes('policy_engine')) return GitBranch;
  if (action.kind === 'readonly') return Activity;
  return Play;
}

function formatUnknown(value: unknown): string {
  if (value === null || value === undefined) return '';
  if (typeof value === 'string') return value;
  if (typeof value === 'number' || typeof value === 'boolean') return String(value);
  return JSON.stringify(value);
}

export function App() {
  const [page, setPage] = useState<PageKey>('overview');
  const [system, setSystem] = useState<SystemInfo | null>(null);
  const [actions, setActions] = useState<ConsoleAction[]>([]);
  const [jobs, setJobs] = useState<Job[]>([]);
  const [status, setStatus] = useState<AgentStatus | null>(null);
  const [skills, setSkills] = useState<AgentSkills | null>(null);
  const [doctor, setDoctor] = useState<string>('');
  const [evidence, setEvidence] = useState<EvidenceSummary | null>(null);
  const [activeJob, setActiveJob] = useState<Job | null>(null);
  const [jobLog, setJobLog] = useState('');
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState('');

  async function refreshCore() {
    setLoading(true);
    setError('');
    try {
      const [systemInfo, actionInfo, jobInfo, evidenceInfo] = await Promise.all([
        api.system(),
        api.actions(),
        api.jobs(),
        api.evidence()
      ]);
      setSystem(systemInfo);
      setActions(actionInfo.actions);
      setJobs(jobInfo.jobs);
      setEvidence(evidenceInfo);
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      setLoading(false);
    }
  }

  async function refreshAgent() {
    setError('');
    try {
      const [statusInfo, skillInfo] = await Promise.all([api.status(), api.skills()]);
      setStatus(statusInfo);
      setSkills(skillInfo);
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  }

  async function refreshDoctor() {
    setError('');
    try {
      const doctorInfo = await api.doctor();
      setDoctor(doctorInfo.raw);
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  }

  useEffect(() => {
    void refreshCore();
    void refreshAgent();
  }, []);

  const mutatingRunning = jobs.some((job) => ['queued', 'running'].includes(job.status) && ['demo', 'lab', 'cleanup'].includes(job.kind));

  async function runAction(action: ConsoleAction) {
    if (action.requires_confirm) {
      const ok = window.confirm(`${actionTitle(action)}\n\n安全说明：${action.safe_description}\n\n风险边界：${action.risk_description}`);
      if (!ok) return;
    }
    setError('');
    setJobLog('');
    try {
      const { job } = await api.startAction(action.id);
      setActiveJob(job);
      setJobs((current) => [job, ...current.filter((item) => item.job_id !== job.job_id)].slice(0, 50));
      const source = new EventSource(`/api/jobs/${job.job_id}/stream`);
      source.addEventListener('snapshot', (event) => {
        const data = JSON.parse((event as MessageEvent).data) as { job: Job; tail: string };
        setActiveJob(data.job);
        setJobLog(data.tail || '');
      });
      source.addEventListener('output', (event) => {
        const data = JSON.parse((event as MessageEvent).data) as { chunk: string; job: Job };
        setActiveJob(data.job);
        setJobLog((current) => `${current}${data.chunk}`.slice(-1024 * 1024));
      });
      source.addEventListener('done', (event) => {
        const data = JSON.parse((event as MessageEvent).data) as { job: Job; chunk: string };
        setActiveJob(data.job);
        if (data.chunk) setJobLog((current) => `${current}${data.chunk}`.slice(-1024 * 1024));
        source.close();
        void api.jobs().then((jobInfo) => setJobs(jobInfo.jobs));
      });
      source.onerror = () => {
        source.close();
      };
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  }

  async function cancelActiveJob() {
    if (!activeJob || !['queued', 'running'].includes(activeJob.status)) return;
    const { job } = await api.cancelJob(activeJob.job_id);
    setActiveJob(job);
    await refreshCore();
  }

  return (
    <div className="app-shell">
      <aside className="sidebar">
        <div className="brand">
          <span className="brand-mark">EP</span>
          <div>
            <strong>EulerPilot</strong>
            <small>Web Console v1</small>
          </div>
        </div>
        <nav aria-label="Main navigation">
          {pages.map((item) => {
            const Icon = item.icon;
            return (
              <button key={item.key} className={page === item.key ? 'nav-item active' : 'nav-item'} onClick={() => setPage(item.key)}>
                <Icon size={17} />
                <span>{item.label}</span>
              </button>
            );
          })}
        </nav>
      </aside>

      <main className="main">
        <header className="topbar">
          <div>
            <p className="eyebrow">证据优先 · 白名单演示 · 旁路控制台</p>
            <h1>{pages.find((item) => item.key === page)?.label}</h1>
          </div>
          <div className="topbar-actions">
            <StatusPill label={system?.host || 'host'} tone="neutral" />
            <StatusPill label={system?.git.head || 'git'} tone={system?.git.dirty ? 'warn' : 'ok'} />
            <button className="icon-button" onClick={() => void refreshCore()} title="刷新控制台状态" aria-label="刷新控制台状态">
              <RefreshCw size={17} className={loading ? 'spin' : ''} />
            </button>
          </div>
        </header>

        {error && <div className="banner error">{error}</div>}

        {page === 'overview' && <Overview system={system} evidence={evidence} status={status} jobs={jobs} />}
        {page === 'skills' && <SkillsPage status={status} skills={skills} doctor={doctor} onRefreshAgent={refreshAgent} onDoctor={refreshDoctor} />}
        {page === 'scheduling' && <SchedulingPage evidence={evidence} />}
        {page === 'extensions' && <ExtensionsPage evidence={evidence} />}
        {page === 'policy' && <PolicyPage transactions={evidence?.policy_timeline || []} />}
        {page === 'evidence' && (
          <EvidenceDemoPage
            evidence={evidence}
            actions={actions}
            jobs={jobs}
            activeJob={activeJob}
            jobLog={jobLog}
            mutatingRunning={mutatingRunning}
            onRun={runAction}
            onCancel={cancelActiveJob}
          />
        )}
      </main>
    </div>
  );
}

function StatusPill({ label, tone }: { label: string; tone: 'ok' | 'warn' | 'bad' | 'neutral' }) {
  return <span className={`status-pill ${tone}`}>{label}</span>;
}

function MetricCard({ label, value, detail, tone = 'neutral' }: { label: string; value: string | number; detail?: string; tone?: 'ok' | 'warn' | 'bad' | 'neutral' }) {
  return (
    <section className={`metric-card ${tone}`}>
      <span>{label}</span>
      <strong>{value}</strong>
      {detail && <small>{detail}</small>}
    </section>
  );
}

function Overview({ system, evidence, status, jobs }: { system: SystemInfo | null; evidence: EvidenceSummary | null; status: AgentStatus | null; jobs: Job[] }) {
  const latestJob = jobs[0];
  return (
    <div className="page-grid">
      <div className="metrics-grid">
        <MetricCard label="证据条目" value={evidence?.total ?? '-'} detail="final evidence compact" tone={evidence?.required_missing === 0 ? 'ok' : 'bad'} />
        <MetricCard label="必需缺失" value={evidence?.required_missing ?? '-'} detail="strict evidence gate" tone={evidence?.required_missing === 0 ? 'ok' : 'bad'} />
        <MetricCard label="Agent 状态" value={status?.ok ? '可读取' : '待刷新'} detail="status --json" tone={status?.ok ? 'ok' : 'warn'} />
        <MetricCard label="最近任务" value={latestJob ? jobStatusZh[latestJob.status] : '无'} detail={latestJob?.action_id || '当前会话暂无任务'} tone={statusTone(latestJob?.status)} />
      </div>

      <section className="panel">
        <div className="panel-heading">
          <h2>平台路径分工</h2>
          <Shield size={18} />
        </div>
        <div className="role-grid">
          <div>
            <span>SP3 official path</span>
            <strong>{system?.path_roles.sp3 || 'loading'}</strong>
          </div>
          <div>
            <span>sched_ext/scx</span>
            <strong>{system?.path_roles.scx || 'loading'}</strong>
          </div>
        </div>
        <div className="capability-row">
          {Object.entries(system?.capabilities || {}).map(([key, value]) => (
            <StatusPill key={key} label={`${capabilityLabelZh[key] || key}:${value ? '可用' : '不可用'}`} tone={value ? 'ok' : 'warn'} />
          ))}
        </div>
      </section>

      <section className="panel">
        <div className="panel-heading">
          <h2>仓库与系统</h2>
          <GitBranch size={18} />
        </div>
        <dl className="kv-list">
          <dt>主机</dt><dd>{system?.host || '-'}</dd>
          <dt>OS</dt><dd>{system?.os.pretty_name || '-'}</dd>
          <dt>Kernel</dt><dd>{system?.os.kernel || '-'}</dd>
          <dt>Git HEAD</dt><dd>{system?.git.head || '-'}</dd>
          <dt>工作区</dt><dd>{system?.git.dirty ? '有未提交变更' : '干净'}</dd>
        </dl>
      </section>
    </div>
  );
}

function SkillsPage({ status, skills, doctor, onRefreshAgent, onDoctor }: { status: AgentStatus | null; skills: AgentSkills | null; doctor: string; onRefreshAgent: () => Promise<void>; onDoctor: () => Promise<void> }) {
  const skillRows = status?.json?.skills || [];
  return (
    <div className="page-grid">
      <div className="toolbar">
        <button className="primary-button" onClick={() => void onRefreshAgent()}><RefreshCw size={16} />刷新 Agent</button>
        <button className="secondary-button" onClick={() => void onDoctor()}><ListChecks size={16} />运行 doctor</button>
      </div>
      <section className="panel">
        <div className="panel-heading">
          <h2>已注册 Skills</h2>
          <Layers3 size={18} />
        </div>
        <div className="chip-list">
          {(skills?.skills || []).map((skill) => <span className="chip" key={skill}>{skill}</span>)}
          {!skills?.skills?.length && <span className="muted">等待读取 list-skills</span>}
        </div>
      </section>
      <section className="panel">
        <div className="panel-heading">
          <h2>状态 JSON</h2>
          <Braces size={18} />
        </div>
        <table className="data-table">
          <thead><tr><th>Skill</th><th>可用</th><th>运行中</th><th>状态</th></tr></thead>
          <tbody>
            {skillRows.map((row, index) => (
              <tr key={`${formatUnknown(row.name)}-${index}`}>
                <td>{formatUnknown(row.name)}</td>
                <td><StatusPill label={boolLabel(row.available)} tone={statusTone(Boolean(row.available))} /></td>
                <td><StatusPill label={boolLabel(row.running)} tone={statusTone(Boolean(row.running))} /></td>
                <td>{formatUnknown(row.state)}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </section>
      <section className="panel">
        <div className="panel-heading">
          <h2>Doctor 输出</h2>
          <SquareTerminal size={18} />
        </div>
        <pre className="log-box">{doctor || '点击“运行 doctor”读取当前能力探测。'}</pre>
      </section>
    </div>
  );
}

function SchedulingPage({ evidence }: { evidence: EvidenceSummary | null }) {
  const perf = evidence?.groups.find((group) => group.name === 'Performance')?.entries || [];
  return (
    <div className="page-grid">
      <section className="panel">
        <div className="panel-heading">
          <h2>性能证据</h2>
          <Activity size={18} />
        </div>
        <EvidenceMiniTable entries={perf} />
      </section>
      <section className="panel">
        <div className="panel-heading">
          <h2>最终图表</h2>
          <BarChart3 size={18} />
        </div>
        <div className="figure-grid">
          {(evidence?.figures || []).map((figure) => (
            <figure key={figure.name}>
              <img src={figure.url} alt={figure.name} />
              <figcaption>{figure.name}</figcaption>
            </figure>
          ))}
        </div>
      </section>
    </div>
  );
}

function ExtensionsPage({ evidence }: { evidence: EvidenceSummary | null }) {
  const [tab, setTab] = useState<ExtensionTab>('network');
  const groupName = tab === 'network' ? 'Network Policy' : tab === 'security' ? 'Security Policy' : 'Resource Control';
  const entries = evidence?.groups.find((group) => group.name === groupName)?.entries || [];
  return (
    <div className="page-grid">
      <div className="segmented">
        <button className={tab === 'network' ? 'active' : ''} onClick={() => setTab('network')}><Network size={16} />网络</button>
        <button className={tab === 'security' ? 'active' : ''} onClick={() => setTab('security')}><Shield size={16} />安全</button>
        <button className={tab === 'resource' ? 'active' : ''} onClick={() => setTab('resource')}><Database size={16} />资源</button>
      </div>
      <section className="panel">
        <div className="panel-heading">
          <h2>{groupLabelZh[groupName]}</h2>
          <FileText size={18} />
        </div>
        <EvidenceMiniTable entries={entries} />
      </section>
    </div>
  );
}

function PolicyPage({ transactions }: { transactions: PolicyTransaction[] }) {
  return (
    <div className="page-grid">
      {transactions.length === 0 && <section className="panel"><p className="muted">当前没有可解析的 transaction_id 时间线，仍可通过“证据与现场演示”页面查看原始结果。</p></section>}
      {transactions.map((tx) => (
        <section className="panel" key={`${tx.source_dir}-${tx.transaction_id}`}>
          <div className="panel-heading">
            <h2>{tx.transaction_id}</h2>
            <GitBranch size={18} />
          </div>
          <p className="muted">{tx.source_dir}</p>
          <ol className="timeline">
            {tx.events.slice(0, 12).map((event, index) => (
              <li key={`${event.file}-${index}`}>
                <span>{event.skill || event.file}</span>
                <strong>{event.stage || event.action || 'event'}</strong>
                <small>{event.policy_id || formatUnknown(event.target)}</small>
              </li>
            ))}
          </ol>
        </section>
      ))}
    </div>
  );
}

function EvidenceDemoPage({ evidence, actions, jobs, activeJob, jobLog, mutatingRunning, onRun, onCancel }: {
  evidence: EvidenceSummary | null;
  actions: ConsoleAction[];
  jobs: Job[];
  activeJob: Job | null;
  jobLog: string;
  mutatingRunning: boolean;
  onRun: (action: ConsoleAction) => Promise<void>;
  onCancel: () => Promise<void>;
}) {
  const actionMap = useMemo(() => new Map(actions.map((action) => [action.id, action])), [actions]);
  return (
    <div className="page-grid">
      <section className="panel">
        <div className="panel-heading">
          <h2>证据清单</h2>
          <BadgeCheck size={18} />
        </div>
        <div className="metrics-grid compact">
          <MetricCard label="Total" value={evidence?.total ?? '-'} />
          <MetricCard label="缺失" value={evidence?.required_missing ?? '-'} tone={evidence?.required_missing === 0 ? 'ok' : 'bad'} />
          <MetricCard label="警告" value={evidence?.warnings ?? '-'} tone={evidence?.warnings === 0 ? 'ok' : 'warn'} />
        </div>
        <div className="evidence-groups">
          {(evidence?.groups || []).map((group) => (
            <details key={group.name} open={group.entries.length > 0 && group.name !== 'Rollback / Cleanup'}>
              <summary>{groupLabelZh[group.name] || group.name}<span>{group.entries.length}</span></summary>
              <EvidenceMiniTable entries={group.entries} />
            </details>
          ))}
        </div>
      </section>

      <section className="panel">
        <div className="panel-heading">
          <h2>推荐演示</h2>
          <Play size={18} />
        </div>
        <ActionGrid ids={recommendedActions} actionMap={actionMap} mutatingRunning={mutatingRunning} onRun={onRun} />
      </section>

      <section className="panel">
        <div className="panel-heading">
          <h2>高级 / 可选</h2>
          <Square size={18} />
        </div>
        <ActionGrid ids={advancedActions} actionMap={actionMap} mutatingRunning={mutatingRunning} onRun={onRun} />
      </section>

      <section className="panel">
        <div className="panel-heading">
          <h2>任务控制台</h2>
          <SquareTerminal size={18} />
        </div>
        {activeJob && (
          <div className="job-head">
            <StatusPill label={jobStatusZh[activeJob.status]} tone={statusTone(activeJob.status)} />
            <span>{activeJob.action_id}</span>
            {['queued', 'running'].includes(activeJob.status) && (
              <button className="danger-button" onClick={() => void onCancel()}><Square size={15} />取消</button>
            )}
          </div>
        )}
        <pre className="log-box tall">{jobLog || '运行 demo 后实时日志会显示在这里。'}</pre>
      </section>

      <section className="panel">
        <div className="panel-heading">
          <h2>最近任务</h2>
          <ListChecks size={18} />
        </div>
        <table className="data-table">
          <thead><tr><th>动作</th><th>类型</th><th>状态</th><th>退出码</th><th>日志</th></tr></thead>
          <tbody>
            {jobs.map((job) => (
              <tr key={job.job_id}>
                <td>{actionTitleZh[job.action_id] || job.action_id}</td>
                <td>{kindLabelZh[job.kind]}</td>
                <td><StatusPill label={jobStatusZh[job.status]} tone={statusTone(job.status)} /></td>
                <td>{job.exit_code ?? '-'}</td>
                <td>{job.log_file}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </section>
    </div>
  );
}

function ActionGrid({ ids, actionMap, mutatingRunning, onRun }: { ids: string[]; actionMap: Map<string, ConsoleAction>; mutatingRunning: boolean; onRun: (action: ConsoleAction) => Promise<void> }) {
  return (
    <div className="action-grid">
      {ids.map((id) => {
        const action = actionMap.get(id);
        if (!action) return <div className="action-card missing" key={id}>{id} 不可用</div>;
        const disabled = mutatingRunning && isMutating(action);
        const Icon = actionIcon(action);
        return (
          <button key={id} className={`action-card ${action.kind}`} disabled={disabled} onClick={() => void onRun(action)}>
            <Icon size={17} />
            <strong>{actionTitle(action)}</strong>
            <small className="action-meta">{kindLabelZh[action.kind]} · {commandKind(action)}</small>
            <span>{action.safe_description}</span>
          </button>
        );
      })}
    </div>
  );
}

function EvidenceMiniTable({ entries }: { entries: EvidenceEntry[] }) {
  if (entries.length === 0) return <p className="muted">暂无该分组证据。</p>;
  return (
    <table className="data-table evidence-table">
      <thead><tr><th>证据名称</th><th>主机</th><th>状态</th><th>路径</th></tr></thead>
      <tbody>
        {entries.map((entry, index) => (
          <tr key={`${entry.path}-${index}`}>
            <td>
              <strong>{entry.name}</strong>
              <small>{entry.notes}</small>
            </td>
            <td>{entry.host}</td>
            <td><StatusPill label={entry.status || (entry.exists ? 'present' : 'missing')} tone={statusTone(entry.status || entry.exists)} /></td>
            <td><code>{entry.path}</code></td>
          </tr>
        ))}
      </tbody>
    </table>
  );
}
