# EulerPilot 提交清单

更新时间：`2026-07-06`

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
- [x] `network_xdp` isolated-veth generic XDP ICMP + TCP + UDP 三规则闭环与 per-rule 统计
- [x] `network_xdp` isolated-veth 协议、源/目的 IP、源/目的端口多字段匹配双机通过：新增 UDP tuple `10.89.0.2:39094 -> 10.89.0.1:19094`，rollback 事件输出 `protocol/src_ip/dst_ip/src_port/dst_port/drop_count/byte_count`
- [x] `network_xdp` real Pod host veth 协议、源/目的 IP、源/目的端口多字段匹配双机通过：新增 UDP tuple `pod_ip:39094 -> bridge_ip:19094`，rollback 事件输出真实 Pod tuple 字段证据
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
- [x] `security_policy` `lsm_ptrace_traceme`、`lsm_capable`、`lsm_task_fix_setuid`、`lsm_task_fix_setgid`、`lsm_task_fix_setgroups` 与 `lsm_cred_prepare` 均要求 scoped cgroup target，分别验证 ptrace、CAP_SYS_ADMIN、setuid、setgid、setgroups credential 转换与 cred_prepare credential preparation 阻断
- [x] `security_policy` 服务联动 anomaly 121/122 pass：`burst_connect`、`burst_openat_sensitive`、`capability_abuse` 均输出 `operation=anomaly/result=observed`，结果目录为 `results/security_policy/anomaly-rules-20260703-121-v4` 与 `results/security_policy/anomaly-rules-20260703-122-v2`
- [x] `security_policy` anomaly 进程过滤 121/122 pass：`comm_prefix=python` 的 `/etc` openat burst 正向规则输出 anomaly，`comm=nohit-proc` 的负向规则不输出 anomaly，结果目录为 `results/security_policy/anomaly-process-filter-20260703-121-v1` 与 `results/security_policy/anomaly-process-filter-20260703-122-v1`
- [x] `security_policy` anomaly 组合 scope 过滤 121/122 pass：scope 外 Python `/etc` open burst 不触发；目标 cgroup 内同一 workload 同时匹配 `target_ref/path_prefix/comm_prefix` 后输出 anomaly，结果目录为 `results/security_policy/anomaly-combo-scope-20260703-121-v1` 与 `results/security_policy/anomaly-combo-scope-20260703-122-v1`
- [x] `security_policy` credential 生命周期 anomaly 121/122 pass：`credential_churn` 输出 `credential_stage`、`uid` 和 cred hit 细节，结果目录为 `results/security_policy/credential-anomaly-20260703-121-v4` 与 `results/security_policy/credential-anomaly-20260703-122-v4`
- [x] `security_policy` credential deep hook 评估 121/122 pass：`lsm_cred_alloc_blank/lsm_cred_transfer` 已 scoped 配置并随 Agent attach，`hook_type` 映射保证同 cgroup 多 credential 规则不串错；普通用户态 workload 下 deep hook hit=0，结果明确记录为评估边界
- [x] `resource_control` CPU+Memory+IO 自动闭环 121/122 验证：YAML v2 `controllers + profiles`、`cpu.max`、`memory.high/low/max`、`io.weight/io.max`、事务化写入、`AuditBus`、`ActionJournal` 和 Agent stop rollback
- [x] `resource_control` `target_ref` cgroup 最小闭环 121/122 验证：`targets + profiles.<name>.target_ref`、目标 cgroup 限制、非目标 cgroup 不误改、审计和 Agent JSONL 携带 `target_ref`
- [x] `resource_control` runtime target 解析闭环 121/122 验证：`type: container_id/container/k8s_pod` 均能解析到目标 cgroup，并复用 CPU/Memory 控制器写入、审计和 rollback
- [x] `resource_control` 真实 runtime readiness 诊断 121/122 刷新：Podman runtime ready，k3s Kubernetes lab ready；Docker 18.09 daemon 因 cgroup v2 devices controller 问题不作为主验证 runtime
- [x] `resource_control` 真实 Podman container target 121/122 pass：验证 `type: container + container_name + runtime` 解析真实容器 cgroup，写入并恢复 `cpu.max/memory.high`
- [x] `resource_control` 真实 k3s Pod target 121/122 pass：验证 `type: k8s_pod + namespace + pod_name` 解析真实 Pod cgroup，写入并恢复 `cpu.max/memory.high`
- [x] `network_qos` 真实 k3s Pod host veth 121/122 pass：解析 lab Pod host veth，安装 TC/TBF，流量命中，rollback 无 qdisc 残留
- [x] `policy_engine` 真实 k3s Pod 联动 121/122 pass：同一个 `target_ref=lab_pod(type=k8s_pod)` 解析为 Pod cgroup 与 Pod host veth，触发 `security_policy burst_connect -> resource_control cpu.max/memory.high + network_qos 2mbit -> rollback`
- [x] `resource_control` CPU quota 效果指标 121/122 验证：`cpu.stat usage_usec/nr_throttled/throttled_usec` 证明 `cpu.max=10000 100000` 后实际 CPU 使用率下降并触发 throttling
- [x] `resource_control` Redis + background CPU quota Compare Benchmark 121/122 验证：拆分 `default_noisy`、`eulerpilot_no_quota`、`eulerpilot_quota` 三阶段，记录 Redis GET/SET RPS 与 background cgroup CPU 使用率，结论限定为同样 Agent 放置下后台限额效果显著，不写成 Redis 性能提升
- [x] `resource_control` Redis quota profile sweep 121/122 验证：扫描 `max/50%/20%/10%/5%` background `cpu.max`，121 推荐 `quota_05`，122 最佳折中 `quota_10`，跨机保守默认演示 profile 暂定 `cpu.max=10000 100000`
- [x] `resource_control` Nginx quota profile sweep 121/122 验证：使用 `nginx + wrk + background CPU hog` 扫描相同 profile，两端均推荐 `quota_05`，作为 Nginx 场景激进候选
- [x] `resource_control` Mixed Redis+Nginx quota profile sweep 121/122 验证：同一窗口并发 Redis GET/SET 与 Nginx wrk，121 推荐 `quota_20`，122 推荐 `quota_50`，证明混合业务 profile 需要同时满足多前台最低保留率
- [x] `resource_control` Mixed Redis+Nginx multi-resource profile 121/122 验证：比较 CPU/cpuset 与 `cpu.max + cpuset.cpus + memory.low/high` 组合 profile，两端均推荐 `multi_quota50`，并验证 applied/restored 审计事件
- [x] `policy_engine` Security anomaly -> Resource Control 降级联动 121/122 验证：消费 `security_policy burst_execve` anomaly，对显式 background cgroup 写 `cpu.max=10000 100000` 与 `memory.high=1048576`，并验证审计、ActionJournal 和 rollback
- [x] `security_policy_demo` BPF LSM file_open 最小闭环
- [x] `security_policy_demo` BPF LSM attach/deny/rollback 集成测试 121/122 均通过
- [x] Runtime 生命周期收拢与 ActionJournal/AuditBus 最小接入；SIGINT/SIGTERM 通过 graceful shutdown 标志触发主循环提前退出，仍走 `stop_all()` 清理路径
- [x] 121 SP3 编译、集成测试和 22 项质量门禁通过，工程质量收口门禁新增 C++ unit tests，100 轮 smoke 与 5 轮 doctor 继续保留
- [x] SP4 sched_ext 自编译内核复核通过：Redis/Nginx `RUNS=3` 多轮对照、Redis PSI ACTIVE probe、最终质量门禁 `reports/sp4/final_quality_gate_scx_workload_20260706-1214.log`
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
- Network XDP tuple 字段 121：`results/network_policy/xdp-20260703-121-fields-v1`
- Network XDP tuple 字段 122：`results/network_policy/xdp-20260703-122-fields-v1`
- Network XDP real Pod tuple 字段 121：`results/network_policy/real-pod-veth-xdp-20260703-k3s-121-tuple-v1`
- Network XDP real Pod tuple 字段 122：`results/network_policy/real-pod-veth-xdp-20260703-k3s-122-tuple-v1`
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
- Security scoped credential/cred_prepare LSM 121：`results/security_policy/integration-20260624-114838`
- Security scoped credential/cred_prepare LSM 122：`results/security_policy/integration-20260624-115440`
- Security credential anomaly 121：`results/security_policy/credential-anomaly-20260703-121-v4`
- Security credential anomaly 122：`results/security_policy/credential-anomaly-20260703-122-v4`
- Security credential deep hooks 121：`results/security_policy/credential-deep-hooks-20260703-121-v2`
- Security credential deep hooks 122：`results/security_policy/credential-deep-hooks-20260703-122-v2`
- Resource Control CPU+Memory 121：`results/resource_control/integration-20260624-160317`
- Resource Control CPU+Memory 122：`results/resource_control/integration-20260624-160349`
- Resource Control IO 121：`results/resource_control/io-20260624-160008`
- Resource Control IO 122：`results/resource_control/io-20260624-160208`
- Resource Control target_ref 121：`results/resource_control/target-20260624-172139`
- Resource Control target_ref 122：`results/resource_control/target-20260624-172916`
- Resource Control runtime target 121：`results/resource_control/runtime-target-20260630-113310`
- Resource Control runtime target 122：`results/resource_control/runtime-target-20260630-113354`
- Resource Control runtime readiness 121：`results/resource_control/runtime-readiness-20260630-k3s-121`
- Resource Control runtime readiness 122：`results/resource_control/runtime-readiness-20260630-k3s-122`
- Resource Control real runtime target 121：`results/resource_control/real-runtime-target-20260630-podman-121-final2`
- Resource Control real runtime target 122：`results/resource_control/real-runtime-target-20260630-podman-122-final2`
- Resource Control real Pod target 121：`results/resource_control/real-pod-target-20260630-k3s-121-v2`
- Resource Control real Pod target 122：`results/resource_control/real-pod-target-20260630-k3s-122-v1`
- Network QoS real Pod host veth 121：`results/network_policy/real-pod-veth-qos-20260630-k3s-121-v2`
- Network QoS real Pod host veth 122：`results/network_policy/real-pod-veth-qos-20260630-k3s-122-v1`
- Resource Control CPU quota 121：`results/resource_control/cpu-quota-20260625-095030`
- Resource Control CPU quota 122：`results/resource_control/cpu-quota-20260625-095114`
- Resource Control Redis quota Compare Benchmark 121：`results/resource_control/redis-quota-compare-20260625-102426`
- Resource Control Redis quota Compare Benchmark 122：`results/resource_control/redis-quota-compare-20260625-102611`
- Resource Control Redis quota Sweep Benchmark 121：`results/resource_control/redis-quota-sweep-20260626-203131`
- Resource Control Redis quota Sweep Benchmark 122：`results/resource_control/redis-quota-sweep-20260626-203505`
- Resource Control Nginx quota Sweep Benchmark 121：`results/resource_control/nginx-quota-sweep-20260626-210702`
- Resource Control Nginx quota Sweep Benchmark 122：`results/resource_control/nginx-quota-sweep-20260626-211057`
- Resource Control Mixed Redis+Nginx quota Sweep Benchmark 121：`results/resource_control/mixed-quota-sweep-20260627-102503`
- Resource Control Mixed Redis+Nginx quota Sweep Benchmark 122：`results/resource_control/mixed-quota-sweep-20260627-103139`
- Resource Control Mixed Redis+Nginx Multi-Resource Benchmark 121：`results/resource_control/mixed-multi-resource-20260628-211631`
- Resource Control Mixed Redis+Nginx Multi-Resource Benchmark 122：`results/resource_control/mixed-multi-resource-20260628-212132`
- Policy Engine Security -> Resource Control 联动 121：`results/policy_engine/security-resource-20260629-163949`
- Policy Engine Security -> Resource Control 联动 122：`results/policy_engine/security-resource-20260629-164135`
- Policy Engine real Pod Security -> Network + Resource 联动 121：`results/policy_engine/real-pod-security-network-resource-20260630-k3s-121-v1`
- Policy Engine real Pod Security -> Network + Resource 联动 122：`results/policy_engine/real-pod-security-network-resource-20260630-k3s-122-v1`
- Security 服务联动 anomaly 121：`results/security_policy/anomaly-rules-20260703-121-v4`
- Security 服务联动 anomaly 122：`results/security_policy/anomaly-rules-20260703-122-v2`
- Security anomaly process filter 121：`results/security_policy/anomaly-process-filter-20260703-121-v1`
- Security anomaly process filter 122：`results/security_policy/anomaly-process-filter-20260703-122-v1`
- Security anomaly combo scope 121：`results/security_policy/anomaly-combo-scope-20260703-121-v1`
- Security anomaly combo scope 122：`results/security_policy/anomaly-combo-scope-20260703-122-v1`
- Security credential anomaly 121：`results/security_policy/credential-anomaly-20260703-121-v4`
- Security credential anomaly 122：`results/security_policy/credential-anomaly-20260703-122-v4`
- Security credential deep hooks 121：`results/security_policy/credential-deep-hooks-20260703-121-v2`
- Security credential deep hooks 122：`results/security_policy/credential-deep-hooks-20260703-122-v2`

## 当前核心文档

- `docs/final_report_submission.md`：最终报告主稿
- `docs/progress_status.md`：当前进度看板
- `docs/network_policy_skill.md`：Network Policy 设计与实施说明
- `docs/network_pod_veth_target.md`：Pod/veth target 解析预备能力说明
- `docs/security_policy_skill.md`：Security Policy 正式化设计与最小验收入口
- `docs/resource_control_skill.md`：Resource Control CPU+Memory+IO 自动闭环设计与验收入口
- `docs/policy_engine_skill.md`：Policy Engine 跨 Skill 联动、审计与回滚说明
- `docs/skills_yaml_plan.md`：Skills/YAML 控制面规划
- `docs/final_quality_gate.md`：质量门禁说明
- `docs/reference_repos.md`：参考仓库与复用边界
- `docs/defense_final.md`：答辩主文档

## 质量与安全审计

- `scripts/final_quality_gate.sh`：TAP 风格 22 项 P0 质量门禁脚本，新增 `make unit-tests`
- `reports/final_quality_gate_20260706-quality-121.log`：121 最新门禁通过记录，22/22 P0 通过，100 轮 smoke 与 5 轮 doctor 通过
- `configs/final_evidence_manifest.json`：最终证据压缩白名单清单
- `scripts/collect_final_evidence.py`：最终证据压缩报告生成脚本
- `reports/final_evidence_compact.md`：答辩入口压缩报告，当前覆盖 32 个核心证据条目
- `reports/final_evidence_compact.json`：机器可读证据状态，当前 `--strict` 检查必需缺失 0、警告 0
- `docs/final_security_audit.md`：最终安全与质量审计报告

## 当前结论

项目已进入争奖证据收口阶段。当前已完成 Security anomaly -> Policy Engine -> Resource Control 降级、Security anomaly -> Policy Engine -> Network+Resource 联动，以及真实 Pod 版 Network+Resource 联动。121/122 的真实 Podman container target、k3s Pod target、Network QoS Pod host veth、Network XDP Pod host veth、真实 Pod Policy Engine 跨 Skill 联动、服务联动 Security anomaly 规则、anomaly 进程过滤、anomaly 组合 scope 过滤、credential 生命周期 anomaly、credential deep hook scoped attach 评估、isolated-veth XDP ICMP/TCP/UDP + UDP tuple 四规则、real Pod host veth XDP ICMP/TCP/UDP + UDP tuple 四规则均已转为 pass；SP4/123 的 sched_ext 自编译内核、Redis/Nginx RUNS=3 workload 对照和最终质量门禁也已转为 pass。下一步重点是答辩材料冻结、现场演示压测和最终演示脚本彩排。

## v3.1 提交前新增检查

- [x] `make agent` 通过。
- [x] `./build/eulerpilot-agent --validate-config configs/agent.yaml` 通过。
- [x] `./build/eulerpilot-agent --validate-config configs/policy_engine_security_network_resource.yaml` 通过。
- [x] `./build/eulerpilot-agent --status --json` 可输出状态 JSON。
- [x] `tests/integration/test_policy_engine_security_network_resource.sh` 在 121/122 通过。
- [x] `tests/integration/test_policy_engine_security_network_resource.sh --repeat 10` 在 121 通过。
- [x] 失败恢复场景通过：Resource 已写入、Network QoS 写入失败时，Policy Engine 回滚 Resource。
- [x] 结果目录包含 `summary.txt`、`report.md`、四类事件 JSONL、ActionJournal、qdisc/rate 证据。
- [x] `transaction_id` 可串起 security、policy_engine、resource_control、network_qos 和 ActionJournal。
- [x] `demo/demo_all_final.sh --mode live|offline|cleanup` 已完成脚本语法检查；live 依赖 root 环境。
- [x] `scripts/check_sp4_env.sh` 已完成脚本语法检查；SP4/123 已完成 sched_ext 自编译内核增强复核。
- [x] 121/122/本地/GitHub 同步后生成最终一致性日志。

## v3.2 iSulad/isula 与真实 target 检查

- [x] TargetResolver 支持 `runtime=isula` / `runtime=isulad`。
- [x] `tests/integration/test_resource_control_runtime_readiness.sh` 输出 `isula_command/isulad_service/isulad_socket/isula_ps_rc`。
- [x] 真实 runtime target 在 Podman 可用时从 blocked 转 pass，121/122 双机通过。
- [x] Podman/systemd cgroup v2 真实容器 cgroup 解析修正为 PID cgroup fallback，定位 `.../libpod-*.scope/container`。
- [x] 121 最新 `scripts/final_quality_gate.sh` 通过 22/22 P0、100 轮 smoke、5 轮 doctor。
- [x] 真实 Kubernetes Pod target 在 `kubectl + eulerpilot-lab` 可用时从 blocked 转 pass，121/122 已通过。
- [x] Network QoS 真实 Pod host veth 演示 121/122 通过。
- [x] Network XDP 真实 Pod host veth 演示 121/122 通过。
- [x] Security 服务联动 anomaly 规则 121/122 通过。
- [x] Security anomaly 进程过滤 121/122 通过。
- [x] Security anomaly 组合 scope 过滤 121/122 通过。
- [x] Network XDP isolated-veth tuple 字段演示 121/122 通过。
- [x] Network XDP real Pod host veth tuple 字段演示 121/122 通过。
- [x] `python3 scripts/collect_final_evidence.py --strict` 通过，最终证据压缩报告覆盖 32 个核心条目，必需缺失 0、警告 0。
- [x] SP4 Redis/Nginx `RUNS=3` sched_ext workload 复核通过，结果纳入 `configs/final_evidence_manifest.json` 和 `reports/final_evidence_compact.*`。
- [x] Stage G Benchmark 与冻结材料已完成：主 Benchmark 结论冻结，现场演示日志仅作为追加彩排记录。
