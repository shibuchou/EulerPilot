# EulerPilot 最终演示视频录制脚本

更新时间：`2026-07-17`

当前状态：正式演示视频尚未录制。本文是 8-10 分钟录制脚本和命令清单，不代表视频文件已经交付。录制完成后，需要在 `docs/submission_checklist.md`、`docs/final_submission_guide.md` 和提交包中补充视频路径或公开链接。

## 录制前准备

本地建立 Web Console 隧道：

```bash
ssh -L 18080:127.0.0.1:18080 openEuler-2403-LTS-SP4
```

SP4 主验证仓库启动控制台：

```bash
cd /root/EulerPilot
web_console/scripts/run_console.sh --daemon
```

录制窗口建议同时打开：

- 浏览器：`http://127.0.0.1:18080`
- 终端 1：SP4 主验证仓库 `/root/EulerPilot`
- 终端 2：只读展示报告和 evidence

录制前只读检查：

```bash
cd /root/EulerPilot
git rev-parse --short HEAD
uname -r
python3 scripts/collect_final_evidence.py --strict
curl -s http://127.0.0.1:18080/api/health
curl -s http://127.0.0.1:18080/api/system | jq .
curl -s http://127.0.0.1:18080/api/evidence/summary | jq .
```

## 时间线与讲稿

### 0:00-0:45 项目介绍

讲稿：

> 大家好，我们的作品是 EulerPilot，面向 openEuler 的自适应资源管控 Agent。它不是把多个脚本简单拼在一起，而是把 eBPF/PSI 观测、用户态策略决策、Skills 编排、系统控制器执行、审计和 rollback 收到同一条闭环里。当前最终验证线是 openEuler 24.03 LTS SP4；cgroup v2 是发行内核稳定控制路径，sched_ext/scx 路径基于 SP4 官方源码自编译启用 `CONFIG_SCHED_CLASS_EXT` 的内核完成复核。

展示命令：

```bash
cd /root/EulerPilot
cat docs/final_submission_guide.md | sed -n '1,60p'
```

### 0:45-1:40 Overview / 环境状态

讲稿：

> 这里是 Web Console 的 Overview。它只读取已有 CLI、事件日志和 evidence 文件，本身不产生新的性能结论。SP4 主机显示 cgroup v2、PSI、BTF 和 sched_ext 状态；其中 sched_ext 为自编译启用内核复核路径，不代表发行默认内核直接支持。

展示：

- Web Console Overview
- `Host / Kernel / Git HEAD`
- `SP3 历史主路径`
- `sched_ext/scx 增强路径`

备用命令：

```bash
curl -s http://127.0.0.1:18080/api/system | jq '.kernel,.features,.path_roles'
```

### 1:40-2:30 Evidence strict pass

讲稿：

> 这是最终 evidence 白名单。它不是递归扫描全部结果，而是按提交清单固定 37 条核心证据，覆盖质量门禁、Redis/Nginx、SP4 RUNS=5、Network、Security、Resource Control、Policy Engine、Web Console 和 K8s 旁路验证。strict 模式要求必需证据缺失为 0、警告为 0。

展示命令：

```bash
python3 scripts/collect_final_evidence.py --strict
sed -n '1,80p' reports/final_evidence_compact.md
```

### 2:30-3:20 Skills & Agent

讲稿：

> Agent 的扩展点通过 Skills 框架组织。`--list-skills` 可以看到内置能力，`--doctor-skills` 会检查配置、环境和依赖。Web Console 只通过白名单动作调用这些命令，不能执行任意 shell。

展示命令：

```bash
./build/eulerpilot-agent --list-skills
./build/eulerpilot-agent --status --json | jq .
./build/eulerpilot-agent --doctor-skills --config configs/agent.yaml
```

### 3:20-4:20 Scheduling / PSI

讲稿：

> 调度主线分为两类：发行内核上使用 cgroup v2 做稳定控制，自编译 sched_ext 内核上复核 scx 后端兼容性。Redis 场景对 latency-sensitive 混布更敏感，收益更明确；Nginx 场景体现 workload 边界，不能简单说所有 workload 都会提升。

展示命令：

```bash
sed -n '1,120p' results/final/redis-scx-compare-20260708-150702/report.md
sed -n '1,120p' results/final/nginx-scx-compare-20260708-152602/report.md
```

### 4:20-5:30 eBPF Extensions

讲稿：

> EulerPilot 覆盖三个 OS Agent 扩展方向。Network 有 connect4、TC QoS 和 XDP；Security 有 BPF LSM、syscall tracing、服务联动 anomaly 和 credential anomaly；Resource Control 有 CPU、Memory、IO 自动闭环，支持显式 cgroup、container 和 Kubernetes Pod target。

展示：

- Web Console `eBPF Extensions`
- `docs/network_policy_skill.md`
- `docs/security_policy_skill.md`
- `docs/resource_control_skill.md`

备用命令：

```bash
sed -n '1,80p' docs/network_policy_skill.md
sed -n '1,80p' docs/security_policy_skill.md
sed -n '1,80p' docs/resource_control_skill.md
```

### 5:30-6:40 Policy Engine Timeline

讲稿：

> 这里展示跨 Skill 联动。安全异常不是孤立日志，而是进入 Policy Engine，触发 Resource Control 降级和 Network QoS 限速。所有动作通过同一个 transaction_id 串起，并进入 AuditBus 和 ActionJournal，失败时需要事务化回滚。

展示命令：

```bash
sed -n '1,120p' results/policy_engine/security-network-resource-20260706-164539/report.md
grep -h '"transaction_id"' results/policy_engine/security-network-resource-20260706-164539/*.jsonl | head -n 20
```

### 6:40-7:50 Live Demo: policy_engine_lab

讲稿：

> 现场只跑一条代表性 live 链路，避免对真实业务环境做高风险操作。这个脚本会创建 lab cgroup 和 lab veth，只作用于 `ep-*` 这类测试设备；结束后通过 cleanup 回滚。

执行命令：

```bash
sudo tests/integration/test_policy_engine_security_network_resource.sh
```

或在 Web Console 的 `Evidence & Live Demo` 页面点击：

```text
跨 Skill 联动实验 / policy_engine_lab
```

### 7:50-8:30 cleanup

讲稿：

> 演示结束必须清理 lab 资源，包括临时 cgroup、veth、qdisc、事件文件和演示进程。EulerPilot 的核心价值之一是所有控制动作都有可审计 rollback，而不是只追求单次跑通。

执行命令：

```bash
sudo demo/demo_all_final.sh --cleanup
curl -s http://127.0.0.1:18080/api/jobs | jq '.[0:5]'
```

Kubernetes 旁路验证只读残留检查：

```bash
kubectl get all -A -l app.kubernetes.io/part-of=eulerpilot-validation
kubectl get all -A -l eulerpilot.io/owner=web-console
```

### 8:30-9:30 final quality gate

讲稿：

> 最后展示质量门禁。门禁不是只查文件存在，还包括 C++ 单元测试、配置校验、Skills 诊断、Agent smoke、doctor 多轮验证和 evidence strict。最终进入 release 前，仍需要保证没有 `.bak` 等残留、没有敏感信息、没有 K8s 验证残留。

执行命令：

```bash
scripts/final_quality_gate.sh
python3 scripts/collect_final_evidence.py --strict
git status --short
```

## 视频收尾口径

> EulerPilot 当前已经完成 Agent Framework、CPU Scheduling/PSI、Network Policy、Security Policy、Resource Control、Policy Engine、Web Console、Kubernetes 旁路验证、rollback/cleanup 和质量门禁闭环。性能结论保持边界：Redis / latency-sensitive 混布场景收益更明确，Nginx 等场景存在 workload 相关差异。项目价值不是承诺所有 workload 无条件提升，而是能观测、决策、执行、回滚，并用 evidence 链条讲清楚收益和代价。

