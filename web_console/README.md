# EulerPilot Web Console

`web_console/` 是 EulerPilot 的旁路展示与演示控制台，定位为：

```text
Evidence-first + 白名单 Demo + 旁路展示控制台
```

它不替代 C++ Agent，不进入资源管控热路径，不修改 `agent/`、`bpf/`、`sched/` 主干。所有状态、图表和结论均来自 EulerPilot 现有 CLI、测试脚本、事件日志和 evidence 文件；Web Console 本身不产生新的性能结论，不作为实验数据源。

## 目录

- `config/actions.yaml`：白名单动作注册表。
- `backend/src/`：Node.js API、job runner、SSE 日志流和安全检查。
- `frontend/src/`：React + TypeScript 控制台界面。
- `scripts/run_console.sh`：启动入口。
- `scripts/stop_console.sh`：daemon 模式停止入口。
- `docs/api.md`：API 说明。
- `docs/security.md`：安全边界说明。

## 部署

在 SP4 主验证仓库：

```bash
cd /root/EulerPilot/web_console
npm ci
npm run build
cd /root/EulerPilot
web_console/scripts/run_console.sh --daemon
```

本地通过 SSH 隧道访问：

```bash
ssh -L 18080:127.0.0.1:18080 openEuler-2403-LTS-SP4
```

浏览器打开：

```text
http://127.0.0.1:18080
```

只读页面和只读 API 可以直接访问；`demo`、`lab`、`cleanup` 等会触发系统状态变化的动作，即使监听在 loopback，也必须设置一次会话 token：

```bash
export EULERPILOT_CONSOLE_TOKEN="$(openssl rand -hex 16)"
web_console/scripts/run_console.sh --daemon
```

浏览器访问时可在地址栏临时带入 token，前端会写入本机 `localStorage` 并从 URL 中移除：

```text
http://127.0.0.1:18080/?token=<EULERPILOT_CONSOLE_TOKEN>
```

如果未设置 token，Overview、Evidence、Skills 等只读展示仍可用，但跨 Skill 联动实验、cleanup 等按钮会被后端拒绝，返回 `mutation_token_required`。这是为了防止同机其他进程绕过浏览器触发 root 级白名单动作。

## 验证

```bash
cd /root/EulerPilot/web_console
npm run lint
npm run test
npm run build
curl http://127.0.0.1:18080/api/health
curl http://127.0.0.1:18080/api/system
curl http://127.0.0.1:18080/api/evidence/summary
curl http://127.0.0.1:18080/api/jobs
```

## 当前完成状态

- 已支持总览、Skills 与 Agent、调度与 PSI、eBPF 扩展能力、Policy Engine 时间线、证据与现场演示六个页面。
- 已支持 `actions.yaml` 白名单动作、SSE 日志流、最近 job 查询、demo/lab/cleanup 单任务锁。
- 变更类动作需要显式 token 授权；失败、超时或取消后会按 `cleanup_action` 执行受控清理。
- 关键页面已中文优先展示；Shell 脚本、集成测试、质量门禁、清理动作使用明确图标和风险说明。
- 默认只监听 `127.0.0.1:18080`，建议始终通过 SSH 隧道访问。

## 推荐现场按钮

- 环境检查
- 查看 Skills
- Agent 状态 JSON
- Skill 诊断
- 离线证据演示
- 跨 Skill 联动实验
- 清理现场资源
