# EulerPilot 提交清单

更新时间：`2026-06-23`

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
- [x] `TargetResolver` netdev + `k8s_pod` host veth 真实解析预备自测通过
- [x] `security_policy` 正式注册名 + YAML v2 path target
- [x] `security_policy` audit 模式 attach BPF 不阻断并写 `lsm_file_open/lsm_bprm_check_security/sys_enter_execve/sys_enter_openat/sys_enter_connect/sys_enter_ptrace` ringbuf observed hit
- [x] `security_policy` enforce 模式复用 BPF LSM file_open + bprm_check_security 完成 blocked hit、拒绝、恢复、无残留闭环
- [x] `security_policy` 最多 8 项 BPF `target_map` 由 YAML `path/exec_path` 下发，并通过双 `/tmp` 动态目标验证
- [x] `security_policy` LSM blocked 事件通过 BPF `target_index` 映射到单条 YAML `rule_id/target_ref`
- [x] `security_policy` 显式 `cgroup_path` target scope：目标 cgroup 内阻断、scope 外允许
- [x] `security_policy` `type: pid` target 自动解析到 cgroup scope：目标 PID 所在 cgroup 内阻断、scope 外允许
- [x] `security_policy` `type: container_id` target 通过限定 cgroup tree 解析到 cgroup scope：目标 container cgroup 内阻断、scope 外允许
- [x] `security_policy` `type: container` target 通过 runtime CLI 解析 container name 到 cgroup scope：目标 cgroup 内阻断、scope 外允许
- [x] `security_policy` `type: k8s_pod` target 通过 kubectl 查询 Pod UID 并解析 cgroup scope：目标 Pod cgroup 内阻断、scope 外允许
- [x] `security_policy` `lsm_socket_connect` 通过 `dst_ip/dst_port/cgroup_id` 阻断目标 cgroup 内 IPv4 connect，scope 外允许，事件携带 endpoint 证据
- [x] `security_policy` `lsm_bprm_check_security` 通过 `exec_prefix/cgroup_id` 阻断目标 cgroup 内可写目录前缀执行，scope 外允许，事件携带 `exec_prefix` 证据
- [x] `security_policy` `lsm_file_open` 支持 `file_access=write` 与 `path_prefix + file_access=write`，验证目标 cgroup 内读放行、写阻断
- [x] `security_policy` `lsm_ptrace_traceme`、`lsm_capable`、`lsm_task_fix_setuid` 与 `lsm_task_fix_setgid` 均要求 scoped cgroup target，分别验证 ptrace、CAP_SYS_ADMIN、setuid 与 setgid credential 转换阻断
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
- Security target_map 动态路径 121：`results/security_policy/integration-20260621-161943`
- Security target_map 动态路径 122：`results/security_policy/integration-20260621-162111`
- Security 多目标 target_map 121：`results/security_policy/integration-20260621-164838`
- Security 多目标 target_map 122：`results/security_policy/integration-20260621-165001`
- Security 规则级事件标识 121：`results/security_policy/integration-20260621-171904`
- Security 规则级事件标识 122：`results/security_policy/integration-20260621-171957`
- Security cgroup scope 121：`results/security_policy/integration-20260621-173942`
- Security cgroup scope 122：`results/security_policy/integration-20260621-174042`
- Security PID target 121：`results/security_policy/integration-20260621-175927`
- Security PID target 122：`results/security_policy/integration-20260621-180029`
- Security container_id target 121：`results/security_policy/integration-20260621-211502`
- Security container_id target 122：`results/security_policy/integration-20260621-211701`
- Security runtime/Pod target 121：`results/security_policy/integration-20260621-214903`
- Security runtime/Pod target 122：`results/security_policy/integration-20260621-215158`
- Security socket_connect LSM 121：`results/security_policy/integration-20260622-105820`
- Security socket_connect LSM 122：`results/security_policy/integration-20260622-110120`
- Security bprm exec_prefix LSM 121：`results/security_policy/integration-20260622-145403`
- Security bprm exec_prefix LSM 122：`results/security_policy/integration-20260622-145716`
- Security scoped credential LSM 121：`results/security_policy/integration-20260623-203659`
- Security scoped credential LSM 122：`results/security_policy/integration-20260623-204135`

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
- `reports/final_quality_gate_20260623_security_setgid.log`：121 最新门禁通过记录
- `docs/final_security_audit.md`：最终安全与质量审计报告

## 当前结论

项目仍处于争奖增强阶段，不应停留在“最终材料整理”。下一步重点是补强 Network/Security/Resource 的成品化深度：Pod veth、Security cred 类规则/只读目录保护/异常规则、Resource CPU+Memory 自动闭环。
