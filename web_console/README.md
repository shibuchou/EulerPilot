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
