# EulerPilot 下一阶段争奖计划 v2.1

更新时间：`2026-06-18`

参考依据：

- `docs/contest_briefing_reference.md`
- `docs/next_phase_plan_v2.md`
- 用户对 v2 的评估意见
- 当前 121/122 代码、结果和质量门禁状态

## 0. v2.1 定位

v2 已经把项目从“补功能计划”推进为“竞赛交付计划”。v2.1 继续提高目标线：

> 宣讲点名能力全部完成只是基础线。争奖版本必须形成统一 Agent Framework 下的 CPU、Network、Security、Resource 四类正式 Agent，并具备跨 Agent 联合决策、可量化验收、SP4 复核、Kubernetes 非侵入演示和完整回滚证据。

v2.1 替代 `docs/next_phase_plan_v2.md` 作为下一阶段执行口径。v2 保留为历史版本，不再作为排期依据。

当前事实：

- `Skill / SkillRegistry / SkillManager / builtin_skills` 已落地。
- `resource_control / psi_gate / network_policy_demo / security_policy_demo` 已进入统一 Agent 管理。
- Network/Security 当前仍是 demo 级闭环，不能作为最终争奖完成度。
- 121/122 已完成正式结果同步，`bpf/vmlinux.h` 因宿主内核差异允许不同。
- 121 仓库态需要优先修复，当前 `.git` 曾出现空目录导致 `git status` 不可用。

## 1. 总体目标

最终系统主线调整为：

```text
统一 Agent Framework
  -> 公共控制面：TargetResolver / AuditBus / ActionJournal / CapabilityDetector
  -> CPU Scheduling Agent
  -> Network Policy Agent
  -> Security Agent
  -> Resource Control Agent
  -> Policy Engine 跨 Agent 联合决策
  -> SP4 + Kubernetes 实际验证
  -> 量化 Benchmark + 一键演示 + 完整回滚
```

核心原则：

- 不把 demo 命令、demo 文件名和 demo 结果作为最终口径。
- 所有有副作用能力都必须支持 status、audit、rollback。
- 所有策略动作都必须有目标作用域，禁止误伤 SSH、kube-system、主机关键进程和非 lab workload。
- 所有性能结论都必须对比默认 Linux 调度器、固定策略和 EulerPilot 自适应策略。

## 2. 公共控制面优先级

v2.1 阶段 A 不再只是仓库清理，而是公共控制面收口。Network、Security、Resource 和跨 Agent 联动都依赖这些能力。

### 2.1 TargetResolver

统一解析目标对象：

```text
PID
  -> cgroup path / cgroup id
  -> container id
  -> Pod UID
  -> namespace / Pod name
  -> netns / veth / host interface
```

最低实现：

- `pid -> cgroup path`
- `cgroup path -> cgroup id`
- `cgroup path -> container id`，可先基于路径模式解析
- `k8s pod -> cgroup path`，可先支持 `eulerpilot-lab` 约束场景
- `pod/netns -> veth/ifindex`，用于 TC/XDP 安全挂载

### 2.2 AuditBus

统一事件模型，避免每个 Skill 输出不同格式 JSONL。

建议事件格式：

```json
{
  "timestamp": "",
  "event_id": "",
  "skill": "network_policy",
  "policy_id": "",
  "rule_id": "",
  "mode": "audit",
  "target": {
    "pid": 0,
    "cgroup_id": 0,
    "container_id": "",
    "namespace": "",
    "pod": ""
  },
  "operation": "",
  "evidence": {},
  "action": "allow",
  "result": "success",
  "severity": "info"
}
```

各 Skill 可以保留独立事件文件，但字段模型必须统一。

### 2.3 ActionJournal

所有有副作用动作都必须记录可恢复状态：

```text
操作前旧值
操作后新值
BPF link
pinned map
qdisc/class/filter
XDP interface
cgroup path
policy id
是否已经恢复
```

建议路径：

```text
run/eulerpilot/action_journal.json
```

Agent 正常退出、异常恢复或下次启动时，都可以读取 Journal 执行清理。

### 2.4 CapabilityDetector

统一探测环境能力：

- BTF
- ring buffer
- BPF LSM
- XDP native/generic
- TC
- cgroup v2 控制器
- PSI
- sched_ext
- `memory.reclaim`
- Kubernetes 与 cgroup driver

不要让各 Skill 重复探测并输出相互矛盾的结论。

## 3. CPU Scheduling Agent

CPU 是赛题核心，不能只补一个 Throughput-first smoke。

### 3.1 正式实验场景

必须形成三类正式实验：

| 场景 | Workload | 目标 |
|------|----------|------|
| Latency-first | Redis 或 Nginx + CPU 干扰 | 降低 P99/P999 或稳定尾延迟 |
| Throughput-first | 内核编译或压缩 + 前台轻负载 | 提升后台吞吐，同时控制前台代价 |
| Mixed / Adaptive | 延迟任务与批处理任务动态变化 | 证明 Agent 能自动切换策略 |

每个主要场景至少比较：

```text
A. 默认 Linux 调度器
B. 固定 scx 策略
C. EulerPilot 自适应策略
```

### 3.2 Throughput-first 必须输出

- 编译耗时或压缩吞吐
- 前台服务 P99
- CPU 利用率
- 调度等待
- DSQ/class_map 命中
- 策略切换时间
- 后台收益与前台代价

### 3.3 通过线

| 项目 | 通过线 |
|------|--------|
| class_map | 目标 workload 有明确命中统计 |
| DSQ | 各 workload 进入预期 DSQ |
| 自动切换 | 压力出现和消退后完成状态迁移 |
| 稳定性 | 多轮 attach/detach 无挂死 |
| 性能 | 至少一个主要场景有稳定量化收益 |
| 边界 | 对无收益场景明确解释，不伪装优化 |

## 4. Network Policy Agent

Network 目标从 `network_policy_demo` 升级为正式 `network_policy` Skill。

### 4.1 Hook 与 target 模型

不能再使用一个全局 cgroup target 覆盖所有 hook。不同 hook 的挂载对象不同：

| Hook | 合理目标 |
|------|----------|
| `cgroup/connect4` | cgroup 路径或 cgroup id |
| `cgroup_skb` | cgroup |
| `tc_ingress/egress` | 网络接口、Pod veth 或 host veth |
| `xdp` | 网络接口 |
| XDP 规则匹配 | IP、协议、端口、报文特征 |

XDP 不能直接按 cgroup 路径挂载或识别目标进程。XDP 必须挂在专用 netns/veth 或 lab Pod veth 上，不能挂生产管理网卡。

### 4.2 YAML 模型

采用“目标定义 + 规则引用”：

```yaml
network_policy:
  enabled: false
  mode: audit          # observe | audit | enforce

  targets:
    redis_cgroup:
      type: cgroup
      path: /sys/fs/cgroup/eulerpilot/demo-net

    lab_veth:
      type: netdev
      ifname: ep-veth-xdp

    web_pod:
      type: k8s_pod
      namespace: eulerpilot-lab
      name: web-demo

  rules:
    - name: deny_redis_port
      hook: cgroup_connect4
      target_ref: redis_cgroup
      protocol: tcp
      dst_port: 6379
      action: deny

    - name: limit_http_egress
      hook: tc_egress
      target_ref: web_pod
      dst_port: 18080
      rate: 5mbit
      action: limit

    - name: xdp_drop_udp
      hook: xdp
      target_ref: lab_veth
      protocol: udp
      dst_port: 9999
      action: drop
```

### 4.3 QoS 技术路线

固定为低风险混合方案：

```text
eBPF TC classifier
  -> 识别目标 cgroup / Pod / 五元组
  -> 设置 classid 或 mark
  -> HTB/TBF 执行稳定整形
  -> Agent 动态修改速率
```

这样既保留 eBPF 精准识别，又避免短时间内实现不稳定的纯 eBPF shaping。

### 4.4 通过线

| 项目 | 通过线 |
|------|--------|
| connect4 作用域 | 目标 cgroup 被拒绝，非目标 cgroup 100% 正常 |
| QoS | 目标速率与配置值误差不超过 +/-10% |
| 非目标流量影响 | 吞吐不低于基线 90% |
| XDP drop | 测试报文 drop 命中率接近 100% |
| rollback | 连续 20 次挂载/卸载无残留 |

交付物：

- `docs/network_policy_skill.md`
- `scripts/demo_network_policy_product.sh`
- `tests/integration/test_network_policy.sh`
- `results/network_policy/`
- `reports/network_policy_events.jsonl`

## 5. Security Agent

Security 目标从 `security_policy_demo` 升级为正式 `security_policy` Skill。

### 5.1 Syscall tracing

至少固定四类：

- `execve`
- `openat`
- `connect`
- `ptrace` 或 `mount`

BPF 侧只负责低开销采集和过滤，用户态负责事件聚合和规则判断，避免把复杂异常检测逻辑塞进内核热路径。

### 5.2 Runtime anomaly

第一版规则型异常检测即可，但必须可配置、可审计：

- 短时间大量 `execve`
- 敏感路径访问或写入
- 容器内 `ptrace`
- 容器内 `mount`
- 异常外连，和 Network Agent 联动

### 5.3 BPF LSM

至少两类强制控制：

```text
file_open / inode_permission
  -> 敏感文件访问控制

bprm_check_security
  -> 程序执行控制
```

时间允许再增加：

```text
socket_connect
  -> 异常外连控制
```

模式统一为：

```text
observe -> 只采集
audit   -> 规则判断并输出事件
enforce -> 实际拒绝
```

### 5.4 YAML 模型

```yaml
security_policy:
  enabled: false
  mode: audit

  target_ref: lab_pod

  tracing:
    syscalls:
      - execve
      - openat
      - connect
      - ptrace

  anomaly_rules:
    - name: burst_execve
      type: rate
      syscall: execve
      threshold: 20
      window_ms: 1000
      severity: medium

    - name: sensitive_path_write
      type: path
      path_prefix: /proc/sys
      access: write
      severity: high

  lsm_rules:
    - name: deny_core_pattern_write
      hook: file_open
      path: /proc/sys/kernel/core_pattern
      permissions: write
      action: deny

    - name: deny_unknown_exec
      hook: bprm_check_security
      path_prefix: /tmp/eulerpilot-deny/
      action: deny
```

### 5.5 通过线

| 项目 | 通过线 |
|------|--------|
| cgroup 过滤 | 非目标 cgroup 无错误阻断 |
| 异常检测延迟 | 规则触发后 1 秒内形成事件 |
| audit/dry-run | 行为成功，同时产生规则命中事件 |
| enforce | 目标行为返回 EPERM 或明确失败 |
| rollback | 卸载后原操作恢复成功 |
| 压力事件 | 中等事件率下无明显 ringbuf 丢失 |

交付物：

- `docs/security_policy_skill.md`
- `scripts/demo_security_policy_product.sh`
- `tests/integration/test_security_policy.sh`
- `results/security_policy/`
- `reports/security_policy_events.jsonl`

## 6. Resource Control Agent

Resource Control 必须提升到与 Network/Security 同等完成度。

### 6.1 宣讲要求与最低争奖线

| 宣讲要求 | v2.1 最低完成线 |
|----------|-----------------|
| cgroup 动态配额调整 | CPU、Memory、IO 三类控制都具备；CPU + Memory 进入正式自动闭环 |
| 内存回收压力深度感知 | memory PSI、memory.events、memory.stat、major fault、refault、回收指标联合判断 |
| 容器资源利用率优化 | PID -> cgroup -> container -> Pod 映射，根据利用率和压力自动调整 lab Pod 配额 |

### 6.2 控制动作

CPU：

- `cpu.weight`
- `cpu.max`

Memory：

- `memory.high`
- `memory.low`
- `memory.max`
- `memory.reclaim`，环境支持时启用

IO：

- `io.weight`
- `io.max`

其中必须有事务化流程：

```text
读取旧值
  -> 校验新策略
  -> 写入控制器
  -> 验证是否生效
  -> 记录变更
  -> 异常时恢复旧值
```

### 6.3 通过线

| 项目 | 通过线 |
|------|--------|
| CPU quota | 实际 CPU 使用与配置上限基本一致 |
| Memory high | 达阈值后 `memory.events/high` 产生证据 |
| 自动调整 | 压力触发后数秒内完成动作 |
| 防抖 | 稳定期间不频繁切换 |
| Pod 作用域 | 只修改 `eulerpilot-lab` 目标 |
| rollback | 所有 cgroup 原值恢复 |

交付物：

- `docs/resource_control_skill.md`
- `tests/integration/test_resource_control.sh`
- `results/resource_control/`
- `reports/resource_control_events.jsonl`

## 7. 跨 Agent 联动

同一 SkillManager 只能证明多个 Skill 在同一框架里，不等于联合决策。v2.1 必须增加独立联动阶段。

### 7.1 场景一：关键业务保护

```text
Redis P99 上升
+ CPU PSI 升高
+ memory PSI 升高
      |
      v
CPU Scheduling Agent: 切换 Latency-first
      |
      v
Resource Control Agent: 提高 Redis cpu.weight / memory.low，限制后台 cpu.max / memory.high
      |
      v
Network Policy Agent: 提高关键业务 QoS 等级
      |
      v
指标恢复后进入 cooldown 并统一回滚
```

### 7.2 场景二：异常容器处置

```text
Security Agent 检测异常 execve / ptrace / 敏感路径访问
      |
      v
Network Agent: 限制或阻断异常容器外连
      |
      v
Resource Agent: 降低异常容器 CPU / IO 配额
      |
      v
CPU Agent: 将其调度到 background DSQ
      |
      v
Security LSM: 对高危行为 enforce
```

交付物：

- `docs/cross_agent_policy.md`
- `tests/e2e/test_cross_agent_protection.sh`
- `tests/e2e/test_abnormal_container_response.sh`
- `reports/cross_agent_events.jsonl`

## 8. SP4 与 Kubernetes 验证

SP4 验证不能等到 7 月中旬。环境发布后立即做 sched_ext 复核。

排期：

```text
6 月 30 日：创建 123 / 准备镜像
7 月 1 日-7 月 3 日：SP4 能力探测、编译、加载、最小 scx smoke
7 月 4 日-7 月 8 日：Redis / 编译 / 压缩正式实验
```

Kubernetes 完整演示可以放后面，但 SP4 sched_ext 必须第一时间确认：

- 内核配置
- libbpf/clang 兼容性
- sched_ext API 差异
- scx BPF verifier
- DSQ 行为

Kubernetes 约束：

- 只创建 `eulerpilot-lab` namespace
- DaemonSet 默认 observe/audit，不默认 enforce
- Network/Security/Resource 只作用于 lab Pod/cgroup
- rollback 后 kube-system 与现有业务不受影响

## 9. v2.1 排期

### 6 月 18 日-6 月 20 日：公共基础设施

- 修复 Git 仓库态
- `.gitignore` 和提交包清理
- 配置 schema version
- `TargetResolver`
- `AuditBus`
- `ActionJournal`
- `CapabilityDetector`
- 统一 Skill status

阶段 A 不再持续到 6 月 22 日。

### 6 月 21 日-6 月 28 日：Network Policy 完整实现

```text
流量识别
  -> connect4 audit/enforce
  -> TC 分类与 QoS
  -> XDP isolated-veth
  -> audit/status/rollback
  -> Benchmark
```

### 6 月 26 日-7 月 5 日：Security Agent 完整实现

可与 Network 后半段交叉推进：

```text
syscall tracing
  -> cgroup filter
  -> user-space anomaly engine
  -> LSM YAML 规则
  -> audit/enforce
  -> audit/rollback
```

### 6 月 29 日-7 月 8 日：Resource Control 完整实现

- CPU/Memory/IO 控制
- memory pressure score
- 状态机
- Pod/cgroup 映射
- 自动优化实验

### 7 月 1 日-7 月 8 日：SP4 与 sched_ext 正式复核

- 123 能力探测
- scx 编译和 smoke
- Latency-first 正式实验
- Throughput-first 正式实验
- mixed adaptive 实验

### 7 月 9 日-7 月 15 日：Kubernetes 与跨 Agent 联动

- `eulerpilot-lab`
- Pod TargetResolver
- 关键业务保护联动
- 异常容器处置联动
- 不影响 kube-system 验证

### 7 月 16 日-7 月 21 日：完整 Benchmark 和稳定性

- 所有正式实验
- 统计汇总
- attach/detach
- crash recovery
- 长稳测试
- 完整 rollback
- Dashboard 数据收口

### 7 月 22 日：功能冻结

此后原则上不再增加新内核功能。

### 7 月 23 日-7 月 30 日：最终材料

- 技术报告
- PPT
- 演示视频
- 一键 demo
- Release
- 提交记录
- 备用录屏
- 答辩问题库

## 10. 最终质量门禁

最终代码和命令逐步去掉 demo 口径：

```bash
make clean
make -B all

./build/eulerpilot-agent --validate-config configs/agent.yaml
./build/eulerpilot-agent --list-skills
./build/eulerpilot-agent --doctor-skills --config configs/agent.yaml
./build/eulerpilot-agent --status --json
```

门禁分三层：

```text
P0：构建、启动、停止、回滚、安全性
P1：各 Skill 独立集成测试
P2：性能、长稳、跨 Agent 联动
```

新增测试：

```bash
bash tests/integration/test_network_policy.sh
bash tests/integration/test_security_policy.sh
bash tests/integration/test_resource_control.sh
bash tests/integration/test_sched_ext.sh
bash tests/e2e/test_cross_agent_protection.sh
bash tests/e2e/test_full_rollback.sh
```

## 11. 立即执行顺序

1. 修复 121 Git 仓库态，建立干净提交基线。
2. 增加 `.gitignore`，清理 `.bak`、`__pycache__`、临时实验副本。
3. 更新 `docs/skills_yaml_plan.md`，把 target/rule/action/status/audit/journal 模型固定下来。
4. 实现 `TargetResolver / AuditBus / ActionJournal / CapabilityDetector` 的最小版本。
5. 把 `network_policy_demo` 重命名或包装为正式 `network_policy` Skill，先完成 connect4 audit/enforce。
6. 补 TC QoS 和 isolated-veth XDP，不挂生产网卡。
7. 把 `security_policy_demo` 升级为正式 `security_policy` Skill，完成四类 syscall tracing 与两类 LSM。
8. 把 Resource Control 从 CPU weight 扩展到 CPU + Memory 自动闭环，IO 至少可演示可回滚。
9. SP4 发布后立即创建 123 做 sched_ext 复核。
10. 完成两个跨 Agent 联动场景。

一句话结论：

> v2.1 的关键不是继续增加零散 eBPF demo，而是把所有能力纳入统一目标解析、统一审计、统一事务回滚和统一策略联动；Network、Security、Resource 三条线都要达到可配置、可量化、可回滚的正式 Agent 水平，CPU sched_ext 则必须用正式 Benchmark 继续证明创新主线。
