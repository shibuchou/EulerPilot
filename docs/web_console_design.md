# EulerPilot Web Console v1 设计

## Summary

Web Console v1 = `Evidence-first + 白名单 Demo + 旁路展示控制台`。

Web Console 的所有状态、图表和结论均来自 EulerPilot 现有 CLI、测试脚本、事件日志和 evidence 文件；Web Console 本身不产生新的性能结论，不作为实验数据源。

它只负责把现有系统能力以浏览器控制台方式展示出来，并通过白名单动作触发少量可控演示。它不替代 C++ Agent，不进入资源管控热路径，不修改 `agent/`、`bpf/`、`sched/` 主干。

默认部署在 `192.168.1.121:/root/EulerPilot`，监听 `127.0.0.1:18080`，本地通过 SSH 隧道访问。

## 架构

```text
Browser
  -> React Web Console
  -> Node.js API
  -> actions.yaml whitelist
  -> existing EulerPilot CLI / demo scripts / tests
  -> existing JSONL / metrics / evidence files
```

## 页面

- `Overview`：Host、Kernel、Git HEAD、Agent 状态、质量门禁、证据完整度。
- `Skills & Agent`：`list-skills`、`status --json`、`doctor-skills`、配置校验。
- `Scheduling & PSI`：Redis/Nginx、sched_ext/scx、PsiGate、性能图表。
- `eBPF Extensions`：Network、Security、Resource 三类能力用 Tabs 展示。
- `Policy Engine Timeline`：按 `transaction_id` 展示跨 Skill 联动证据链。
- `Evidence & Live Demo`：证据清单、白名单动作、实时日志、cleanup。

## SP3/SP4 路径口径

Overview 不把 SP3 上 `sched_ext` 不可用显示为项目失败，而显示为路径分工：

```text
SP3 official path: cgroup v2 active
sched_ext/scx: enhanced path / SP4 migration target
```

当前 SP3 稳定主路径是 cgroup v2；sched_ext/scx 是增强路径和 SP4 迁移目标。

## Evidence 分组

Evidence 页面按比赛评分关注点分组：

- Agent Framework
- CPU Scheduling / PSI
- Performance
- Network Policy
- Security Policy
- Resource Control
- Policy Engine
- Rollback / Cleanup
- Quality Gate

核心证据来自 `configs/final_evidence_manifest.json` 和 `reports/final_evidence_compact.json`。

## 白名单动作

Recommended Demo：

- `check_env`
- `list_skills`
- `status_json`
- `doctor_skills`
- `demo_offline`
- `policy_engine_lab`
- `demo_cleanup`

Advanced / Optional：

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
7. Evidence 能按评分项展示 32 条证据。
8. `policy_engine_lab` 可作为主演示链路。
9. `demo_cleanup` 可清理现场资源。
10. `scripts/final_quality_gate.sh` 仍能独立运行。
