# Web Console API

所有 API 均由 `web_console/backend/src/server.js` 提供。默认监听 `127.0.0.1:18080`。

## Readonly

- `GET /api/health`：控制台服务状态。
- `GET /api/system`：主机、内核、Git HEAD、cgroup v2、PSI、sched_ext/scx 路径分工。
- `GET /api/actions`：返回 `actions.yaml` 白名单动作。
- `GET /api/jobs`：返回最近 50 个 job。
- `GET /api/agent/status`：调用 `build/eulerpilot-agent --status --json`。
- `GET /api/agent/skills`：调用 `build/eulerpilot-agent --list-skills`。
- `GET /api/agent/doctor`：调用 `build/eulerpilot-agent --doctor-skills --config configs/agent.yaml`。
- `GET /api/evidence/summary`：读取 final evidence compact 和 manifest，按评分项分组。
- `GET /api/events?skill=<name>&tail=<n>`：读取 `reports/events/<skill>.jsonl` 尾部事件。

## Job

- `POST /api/actions/:id/start`：启动白名单 action。
- `GET /api/jobs/:job_id`：读取 job 状态。
- `GET /api/jobs/:job_id/stream`：SSE 实时日志流。
- `POST /api/jobs/:job_id/cancel`：取消运行中的 job。

Job 状态字段：

```text
job_id
action_id
kind
status: queued|running|succeeded|failed|canceled|timeout
started_at
ended_at
exit_code
log_tail
log_file
pid
error
```

## 错误

- 非白名单 action 返回 `404`。
- `demo/lab/cleanup` 单任务锁冲突返回 `423`。
- 非 loopback bind 且缺少 token 时，服务拒绝启动。
