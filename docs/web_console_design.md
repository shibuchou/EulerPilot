# EulerPilot Web Console v1 设计

## 摘要

Web Console v1 = `Evidence-first + 白名单 Demo + 旁路展示控制台`。

Web Console 的所有状态、图表和结论均来自 EulerPilot 现有 CLI、测试脚本、事件日志和 evidence 文件；Web Console 本身不产生新的性能结论，不作为实验数据源。

它只负责把现有系统能力以浏览器控制台方式展示出来，并通过白名单动作触发少量可控演示。它不替代 C++ Agent，不进入资源管控热路径，不修改 `agent/`、`bpf/`、`sched/` 主干。

默认部署在 SP4 主验证仓库 `192.168.1.123:/root/EulerPilot`，监听 `127.0.0.1:18080`，本地通过 SSH 隧道访问。`192.168.1.121:/root/EulerPilot` 只作为 SP3 历史验证和回归对照仓库。

## 架构

```text
Browser
  -> React Web Console（中文优先展示）
  -> Node.js API
  -> actions.yaml whitelist
  -> existing EulerPilot CLI / demo scripts / tests
  -> existing JSONL / metrics / evidence files
```

## 页面

- `总览`：Host、Kernel、Git HEAD、Agent 状态、质量门禁、证据完整度。
- `Skills 与 Agent`：`list-skills`、`status --json`、`doctor-skills`、配置校验。
- `调度与 PSI`：Redis/Nginx、sched_ext/scx、PsiGate、性能图表。
- `eBPF 扩展能力`：Network、Security、Resource 三类能力用 Tabs 展示。
- `Policy Engine 时间线`：按 `transaction_id` 展示跨 Skill 联动证据链。
- `证据与现场演示`：证据清单、白名单动作、实时日志、cleanup。

## SP3/SP4 路径口径

Overview 不把 SP3 上 `sched_ext` 不可用显示为项目失败，而显示为路径分工；在 SP4 自编译 sched_ext 内核上则显示增强路径已可用：

```text
SP3 历史主路径：cgroup v2 已启用
sched_ext/scx 增强路径：SP4 官方源码自编译内核迁移目标，或 SP4 自编译启用内核当前可用
```

当前 SP3 历史稳定路径是 cgroup v2；SP4 发行环境已完成适配验证，但发行内核未默认启用 `CONFIG_SCHED_CLASS_EXT`。sched_ext/scx 路径基于 SP4 官方源码自编译启用内核完成复核，用于证明 EulerPilot 的 scx 后端兼容性和迁移能力。

## Evidence 分组

Evidence 页面按比赛评分关注点分组：

- Agent 框架
- CPU 调度 / PSI
- 性能结果
- 网络策略
- 安全策略
- 资源管控
- 策略引擎
- 回滚 / 清理
- 质量门禁

核心证据来自 `configs/final_evidence_manifest.json` 和 `reports/final_evidence_compact.json`。

## 白名单动作

推荐演示：

- `check_env`
- `list_skills`
- `status_json`
- `doctor_skills`
- `demo_offline`
- `policy_engine_lab`
- `demo_cleanup`

高级 / 可选：

- `policy_engine_real_pod`
- `final_quality_gate`

第一版只展示结果、不默认提供按钮：

- `network_qos_tc`
- `network_xdp`
- `security_anomaly`
- `resource_control_io`
- `demo_live`

## 验收标准

1. `web_console/` 独立存在。
2. `agent/`、`bpf/`、`sched/` 主干不改。
3. `actions.yaml` 白名单生效。
4. 非白名单 action 无法执行。
5. SSE 日志可实时显示。
6. `demo/lab/cleanup` 单任务锁生效。
7. Evidence 能按评分项展示37 条 evidence，并随 `configs/final_evidence_manifest.json` 更新。
8. `policy_engine_lab` 可作为主演示链路。
9. `demo_cleanup` 可清理现场资源。
10. `scripts/final_quality_gate.sh` 仍能独立运行。

