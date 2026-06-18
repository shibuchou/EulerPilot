# EulerPilot 下一阶段争奖计划 v2.0

更新时间：`2026-06-18`

> 状态：历史版本。当前下一阶段执行口径已升级为 `docs/next_phase_plan_v2_1.md`，本文件仅用于保留 v2 规划演进记录。

参考依据：

- `docs/contest_briefing_reference.md`
- `docs/next_phase_plan_v1.md`
- 当前 121/122 代码与质量门禁状态

## 0. v2 调整原则

v1 的方向是“把当前高完成度原型继续加固”。v2 的目标改为：

> 以争奖为目标，对齐比赛宣讲中的 Agent Framework、CPU Scheduling Agent、Network Policy、Security Agent、Resource Control 五条主线，把当前 demo 级能力提升为可配置、可回滚、可展示、可量化的系统级 Agent 能力。

当前事实：

- `Skill / SkillRegistry / SkillManager / builtin_skills` 已经落地
- `resource_control / psi_gate / network_policy_demo / security_policy_demo` 已由统一 Agent 管理
- `network_policy_demo` 已有 `cgroup/connect4` 单端口 deny demo
- `security_policy_demo` 已有 `BPF LSM file_open` 单路径 deny demo
- `final_quality_gate.sh` 已有 12 项 P0 门禁，并在 121 上通过

因此下一阶段不再是“先证明可扩展”，而是：

```text
demo 级 Skill
-> 可配置正式 Skill
-> 有事件审计和命中统计
-> 有 dry-run / enforce
-> 有回滚和残留清理
-> 有独立实验脚本和报告
-> 可进入最终答辩主线
```

## 1. 赛题宣讲对齐目标

### 1.1 Agent Framework

必须继续保持：

- 标准化 Skill 接口
- Skill 注册和发现
- YAML 驱动启停
- `--list-skills`
- `--doctor-skills`
- 统一生命周期
- 失败回滚
- 状态查询和报告

新增要求：

- Skill 状态进入统一 status 输出
- Skill 事件进入统一 audit 日志
- Benchmark/report 作为 Tool 能力至少在文档和 CLI 层可枚举

### 1.2 CPU Scheduling Agent

必须保持并增强：

- sched_ext/scx 真实加载
- class_map 由 Agent 写入
- 不同 workload 进入不同 DSQ 或调度路径
- 至少支持 `Latency-first` 和 `Throughput-first`
- 自动策略切换
- 调度统计和 class_map 命中证据

v2 增强重点：

- 让报告明确说明 `cgroup v2` 是 SP3 主交付和兜底后端，`sched_ext/scx` 是 CPU Scheduling Agent 的核心创新路径
- 在 SP4/123 环境发布后复核 sched_ext，不升级或破坏 121
- 补 `Throughput-first` 对内核编译/压缩任务的实验或 smoke 证据

### 1.3 Network Policy Agent

比赛宣讲明确提到：

- 流量分类与精准识别
- 细粒度 QoS 限速策略
- XDP 数据包高速拦截

因此最终不能停留在 `cgroup/connect4` 单端口拒绝。

v2 完成目标：

- `cgroup/connect4`：作为低风险基础能力，支持 cgroup 目标限定、allow/deny、命中计数、dry-run/enforce
- `QoS/TC`：实现至少一种真实限速，建议先做 egress token bucket 或 tc qdisc/class/filter 包装
- `XDP`：实现独立 XDP drop/pass demo，支持动态规则、命中统计、卸载恢复
- 三层能力都由 NetworkPolicySkill 统一配置和展示，不做互相割裂的脚本 demo

### 1.4 Security Agent

比赛宣讲明确提到：

- Syscall Tracing
- 运行时异常行为监测
- LSM 挂钩强制访问控制

因此最终不能停留在固定路径拒绝。

v2 完成目标：

- syscall tracing：至少跟踪 `execve/openat/connect/ptrace/mount` 中的 2-3 类，并按 cgroup/PID 过滤
- runtime anomaly：实现规则型异常检测，例如短时间大量 `execve`、访问敏感路径、容器内 `ptrace/mount`
- BPF LSM：保留 file_open deny，并扩展为 YAML 配置路径/动作/模式
- 支持 `dry-run` 只审计不拦截，`enforce` 执行拒绝
- 输出安全事件 JSONL，包括对象、行为、证据、风险等级、动作

### 1.5 Resource Control Agent

当前已完成 CPU cgroup 权重调整。v2 需要补齐宣讲中的“资源管控”厚度：

- CPU：`cpu.weight` 已有，补 `cpu.max` 可选限额
- Memory：读取 `memory.current / memory.events / memory.stat / memory.high`
- PSI：CPU 已入门控，补 memory/io PSI 进入证据输出
- Container/Pod：识别目标 cgroup，避免误操作系统 cgroup

## 2. 阶段规划

### 阶段 A：6 月 18 日-6 月 22 日，基线整理与口径统一

目标：保证当前已有成果稳定，清理会影响后续 release 的硬问题。

必须完成：

1. 修复 Git 仓库态：当前 121 `.git` 是空目录，`git status` 不可用。需要重建真实 Git 仓库或从本地/远端同步完整 `.git`。
2. 统一评分口径：`next_phase_plan_v1.md` 的 `79/100` 与 `evaluation_report.md` 的 `86/100` 需要说明成“保守审计分”和“现状自评分”。
3. 统一文档口径：`handover_manual.md` 仍有结果目录只在 122 的旧说法，需要更新为 121/122 已同步、122 保留 sched_ext 验证角色。
4. 删除或归档 `.bak`、空 `.git`、`__pycache__` 等不应进入提交包的内容。
5. 增加 `.gitignore`，覆盖 `build/`、`__pycache__/`、`*.bak`、临时 smoke 日志。
6. 确认 121/122 文档、最终结果、7 张 SVG 再次一致。

验收：

```bash
git status --short
make agent
./build/eulerpilot-agent --list-skills
./build/eulerpilot-agent --doctor-skills --config configs/agent.yaml
bash scripts/final_quality_gate.sh
```

### 阶段 B：6 月 23 日-6 月 30 日，NetworkPolicySkill 成品化

目标：从单端口 connect4 demo 升级为符合宣讲要求的 Network Policy Agent。

#### B1. YAML schema

新增或扩展：

```yaml
network_policy:
  enabled: false
  mode: dry-run        # dry-run | enforce
  target:
    type: cgroup       # cgroup | pid | k8s_pod
    cgroup_path: /sys/fs/cgroup/eulerpilot/demo-net
  rules:
    - name: deny_redis_port
      hook: cgroup_connect4
      protocol: tcp
      dst_port: 6379
      action: deny
    - name: limit_http_egress
      hook: tc_egress
      dst_port: 18080
      rate: 5mbit
      action: limit
    - name: xdp_drop_udp
      hook: xdp
      protocol: udp
      dst_port: 9999
      action: drop
  audit:
    enabled: true
    path: reports/network_policy_events.jsonl
  cleanup:
    auto_detach_on_stop: true
    unpin_on_rollback: true
```

#### B2. 功能验收

必须演示：

- `dry-run`：命中规则但不拒绝，事件进入 JSONL
- `enforce`：目标 cgroup 内 connect 被拒绝，外部不受影响
- `QoS`：限速前后吞吐或请求速率发生可测变化
- `XDP`：规则命中 `XDP_DROP`，卸载后恢复
- `rollback`：无 BPF link、pinned map、cgroup 残留

交付物：

- `docs/network_policy_skill.md`
- `scripts/demo_network_policy_product.sh`
- `results/network_policy/`
- `reports/network_policy_events.jsonl`

### 阶段 C：7 月 1 日-7 月 8 日，SecurityPolicySkill 成品化

目标：覆盖 syscall tracing、异常监测和 BPF LSM 三条宣讲要求。

#### C1. YAML schema

```yaml
security_policy:
  enabled: false
  mode: dry-run        # dry-run | enforce
  target:
    type: cgroup
    cgroup_path: /sys/fs/cgroup/eulerpilot/demo-sec
  tracing:
    syscalls:
      - execve
      - openat
      - ptrace
      - mount
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
  audit:
    enabled: true
    path: reports/security_policy_events.jsonl
```

#### C2. 功能验收

必须演示：

- syscall tracing 可按目标 cgroup 过滤
- 运行时异常规则能输出风险事件
- LSM enforce 能拒绝目标行为
- dry-run 不拒绝，只记录
- rollback 后访问恢复
- 无关进程/Pod 不受影响

交付物：

- `docs/security_policy_skill.md`
- `scripts/demo_security_policy_product.sh`
- `results/security_policy/`
- `reports/security_policy_events.jsonl`

### 阶段 D：7 月 9 日-7 月 15 日，Resource Control 与 Workload 感知增强

目标：补齐“不能只靠 comm 匹配”和“资源控制不止 CPU weight”的短板。

必须完成：

1. Workload 分类证据表：输出 comm、wakeup、runtime、ctx_switch、migrate、CPU PSI、memory PSI、IO PSI。
2. 分类策略说明：进程名仅作为辅助证据，动态指标参与评分。
3. Memory pressure 观测：读取 `/proc/pressure/memory`、`memory.current`、`memory.events`。
4. Resource Control 扩展：至少加入 `cpu.max` 或 `memory.high` 的可配置动作。
5. 新增编译或压缩场景 smoke：证明 `Throughput-first` 不只是文档概念。

交付物：

- `docs/workload_classification.md`
- `docs/resource_control_skill.md`
- `bench/compile/` 或 `bench/compress/`
- 更新 `final_report_submission.md`

### 阶段 E：7 月 16 日-7 月 22 日，SP4 与 K8s 验证

目标：把平台化能力从单机演示扩展到官方新环境和集群非侵入验证。

SP4：

- 新建 123，不升级 121
- 验证 openEuler 24.03 LTS SP4 的 sched_ext 可用性
- 若支持，跑 `RUNS>=5` Redis sched_ext compare
- 若不支持，形成兼容性复核报告，不强行包装

K8s：

- 只创建 `eulerpilot-lab` namespace
- DaemonSet 默认 observe-only
- Network/Security 只作用于 lab Pod/cgroup
- 验证 rollback 后 kube-system 和现有业务不受影响

交付物：

- `docs/sp4_sched_ext_validation.md`
- `docs/k8s_validation.md`
- `deploy/k8s/*.yaml`
- `demo/k8s_demo_runbook.md`

### 阶段 F：7 月 23 日-7 月 30 日，冻结与答辩材料

目标：冻结代码和结果，完成最终提交包。

必须完成：

- 最终技术报告
- 答辩 PPT
- 8-10 分钟演示视频
- 一键演示脚本
- Dashboard
- Release tag
- GitHub/Gitee 提交记录
- 开发过程数据整理

最终质量门禁：

```bash
make clean && make -B agent observer network-policy-demo security-policy-demo
./scripts/check_env.sh
./build/eulerpilot-agent --list-skills
./build/eulerpilot-agent --doctor-skills --config configs/agent.yaml
timeout 15s ./build/eulerpilot-agent --config configs/agent.yaml
bash scripts/final_quality_gate.sh
bash scripts/rollback.sh
```

## 3. Network/Security 争奖验收线

### Network Policy 最低争奖线

- cgroup/connect4 allow/deny
- TC 或 cgroup skb QoS 限速
- XDP drop/pass
- 命中计数
- dry-run/enforce
- audit JSONL
- rollback 幂等
- 不影响 SSH 和非目标业务

### Security Agent 最低争奖线

- syscall tracing 至少 2-3 类
- 运行时异常规则至少 2 类
- BPF LSM enforce 至少 1 类
- dry-run/enforce
- audit JSONL
- cgroup/PID 目标过滤
- rollback 后完全恢复
- 不影响无关进程/Pod

## 4. 分数提升策略

| 评分项 | 当前短板 | v2 提升动作 |
|--------|----------|-------------|
| 创新性 | 策略生成和扩展深度不足 | 强化 Agent Framework + Policy Engine 联合决策，Network/Security/Resource 共用 SkillManager |
| 功能完整性 | Network/Security demo 级 | 完成分类、QoS、XDP、Tracing、Anomaly、LSM |
| 性能提升 | sched_ext 收益不均匀 | 保留边界分析，补 Throughput-first 和 SP4 复核 |
| 代码质量 | 注释、测试、仓库态不足 | 注释、.gitignore、真实 Git、smoke/integration tests |
| 演示效果 | 视频和一键演示不足 | 8-10 分钟固定脚本，展示 deny/limit/drop/trace/rollback |

## 5. 红线

- 不破坏 121 主交付快照
- 不覆盖 122 已有 sched_ext 证据
- 不升级 121 到 SP4
- 不把 XDP 直接挂到生产网卡做无保护测试
- 不让 Security LSM 默认 enforce
- 不影响 SSH、kube-system、master 和非 lab workload
- 不把 demo 级能力写成生产级能力，必须用证据支撑

## 6. 立即执行顺序

1. 修 Git 仓库态和 `.gitignore`
2. 统一文档口径与评分基线
3. 写 `docs/network_policy_skill.md` 和 `docs/security_policy_skill.md` 设计细化
4. NetworkPolicySkill：先 dry-run/enforce/audit，再 QoS，再 XDP
5. SecurityPolicySkill：先 syscall tracing/audit，再 anomaly，再 LSM YAML 化
6. 增强 final quality gate，把 Network/Security 产品化演示纳入 P0 或 P1
7. 准备演示视频脚本

一句话结论：

> v2 的重点不是继续包装已有成果，而是把宣讲明确点名的 Network Policy、Security Agent 和 Resource Control 从 demo 推到可配置、可回滚、可审计、可量化的正式 Skill；CPU sched_ext 主线继续作为创新核心，SP4/K8s 作为争奖加分项。
