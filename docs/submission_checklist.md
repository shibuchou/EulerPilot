# EulerPilot 提交清单

更新时间：`2026-06-21`

## 已完成

- [x] SP3 + cgroup v2 主闭环
- [x] OLK-6.6 + sched_ext 正式对照线
- [x] PsiGate v1 闭环验证
- [x] Redis `RUNS=5` 正式候选结果
- [x] Nginx `RUNS=5` 正式候选结果
- [x] Skills 插件化框架：`Skill / SkillRegistry / SkillManager / builtin_skills`
- [x] YAML v2 驱动 Skills 启停与配置：`targets + rules + target_ref`
- [x] `--list-skills / --doctor-skills / --verbose / --jsonl` 命令行
- [x] `network_policy` cgroup/connect4 audit/enforce/rollback 最小闭环
- [x] `network_qos` TC egress classifier + TBF 限速最小闭环
- [x] `network_qos` TC QoS 速率误差 Benchmark 双机通过
- [x] `network_xdp` isolated-veth generic XDP ICMP + TCP 多规则闭环
- [x] `TargetResolver` netdev + `k8s_pod` 诊断型入口自测通过
- [x] `security_policy` 正式注册名 + YAML v2 path target
- [x] `security_policy` audit 模式 attach BPF 不阻断并写 `lsm_file_open/lsm_bprm_check_security/sys_enter_execve/sys_enter_openat/sys_enter_connect/sys_enter_ptrace` ringbuf observed hit
- [x] `security_policy` enforce 模式复用 BPF LSM file_open + bprm_check_security 完成 blocked hit、拒绝、恢复、无残留闭环
- [x] `security_policy_demo` BPF LSM file_open 最小闭环
- [x] `security_policy_demo` BPF LSM attach/deny/rollback 集成测试 121/122 均通过
- [x] Runtime 生命周期收拢与 ActionJournal/AuditBus 最小接入
- [x] 121 SP3 编译、集成测试和 17 项质量门禁通过，最新 100 轮 smoke 与 5 轮 doctor 通过
- [x] 静态 Dashboard：`reports/dashboard/index.html`
- [x] Prometheus `/metrics` 端点：默认关闭，监听 `127.0.0.1:9108`
- [x] 中文最终报告主稿与答辩材料

## 当前核心结果目录

- Redis：`results/final/redis-scx-compare-20260612-191543`
- Nginx：`results/final/nginx-scx-compare-20260612-194018`
- Network connect4：`results/network_policy/integration-20260619-142347`
- Network TC QoS：`results/network_policy/qos-tc-20260619-142357`
- Network TC QoS Benchmark 121：`results/network_policy/qos-rate-20260620-181708`
- Network TC QoS Benchmark 122：`results/network_policy/qos-rate-20260620-181755`
- Network XDP 多规则 121：`results/network_policy/xdp-20260620-183031`
- Network XDP 多规则 122：`results/network_policy/xdp-20260620-184212`
- Security BPF LSM demo 121：`results/security_policy/integration-20260621-095537`
- Security BPF LSM demo 122：`results/security_policy/integration-20260621-100937`
- Security 正式 audit/enforce 121：`results/security_policy/integration-20260621-101929`
- Security 正式 audit/enforce 122：`results/security_policy/integration-20260621-103431`
- Security BPF ringbuf hit 121：`results/security_policy/integration-20260621-104254`
- Security BPF ringbuf hit 122：`results/security_policy/integration-20260621-105602`
- Security syscall tracing 121：`results/security_policy/integration-20260621-110631`
- Security syscall tracing 122：`results/security_policy/integration-20260621-113455`
- Security 四类 syscall tracing 121：`results/security_policy/integration-20260621-150713`
- Security 四类 syscall tracing 122：`results/security_policy/integration-20260621-151229`
- Security 双 LSM enforce 121：`results/security_policy/integration-20260621-152838`
- Security 双 LSM enforce 122：`results/security_policy/integration-20260621-153514`

## 当前核心文档

- `docs/final_report_submission.md`：最终报告主稿
- `docs/progress_status.md`：当前进度看板
- `docs/network_policy_skill.md`：Network Policy 设计与实施说明
- `docs/network_pod_veth_target.md`：Pod/veth target 解析预备能力说明
- `docs/security_policy_skill.md`：Security Policy 正式化设计与最小验收入口
- `docs/skills_yaml_plan.md`：Skills/YAML 控制面规划
- `docs/final_quality_gate.md`：质量门禁说明
- `docs/reference_repos.md`：参考仓库与复用边界
- `docs/defense_final.md`：答辩主文档

## 质量与安全审计

- `scripts/final_quality_gate.sh`：TAP 风格 17 项 P0 质量门禁脚本
- `reports/final_quality_gate_20260621_security_bprm.log`：121 最新门禁通过记录
- `docs/final_security_audit.md`：最终安全与质量审计报告

## 当前结论

项目仍处于争奖增强阶段，不应停留在“最终材料整理”。下一步重点是补强 Network/Security/Resource 的成品化深度：Pod veth、Security 动态 target map、容器级 target 绑定、Resource CPU+Memory 自动闭环。
