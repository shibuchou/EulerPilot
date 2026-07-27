# openEuler 24.03 LTS SP4 验证计划

更新时间：`2026-07-27`

SP4 已接入为 EulerPilot 后续完整能力验证平台。本文件记录 SP4 初始验证结果、sched_ext/scx 自编译内核验证结果和 v6 封版收口入口；121 的 SP3 结果作为比赛强制兼容环境继续保留，122 只作为历史 OLK/sched_ext 对照。

## 当前结果

- SP4 主机：`openEuler-2403-LTS-SP4` / `192.168.1.123`
- 仓库路径：`/root/EulerPilot`
- 验证基线：v6 收口分支需重新形成 `tested_candidate_commit`，并通过 Candidate Gate 后确认 `tested_code_commit`
- 系统版本：`openEuler 24.03 LTS SP4`
- 初始发行内核：`6.6.0-159.4.3.154.oe2403sp4.x86_64`
- sched_ext 验证内核：`6.6.0-159.4.3.154.oe2403sp4.x86_64-eulerpilot-scx`
- 已启用启动参数：`systemd.unified_cgroup_hierarchy=1 cgroup_no_v1=all psi=1`
- cgroup v2：已挂载，controllers 包含 `cpu io memory`
- PSI：`cpu/memory/io` 已可用
- BTF / BPF LSM / TC / XDP / sched_ext：能力探测可用
- Web Console：历史构建已通过 `npm ci/test/lint/build/audit`；当前 Evidence 为 42 条、必需缺失 0、警告 0
- 质量门禁：历史 SP4 日志保留；v6 后已在 `/root/EulerPilot` 通过缩短版 preflight `29/29 P0`。SP4 final gate 已在 candidate-bound gates 和 formal artifact 后完成。
- v3.1 主链路：`tests/integration/test_policy_engine_security_network_resource.sh --repeat 10` 已在 SP4 sched_ext 内核通过
- sched_ext workload：旧 Redis/Nginx RUNS=10、pressure gradient、static-vs-agent、throughput-first、mixed-adaptive 和 Agent overhead 已降级为 provisional/invalid historical。正式结果已经使用 formal artifact 运行；后续复测仍必须使用 `/root/eulerpilot-artifacts/<tested_code_commit>/<artifact_id>/`，不能直接使用固定 `/usr/local/bin/scx_eulerpilot`。
- Kubernetes 旁路验证：已通过 `k8s-master` 只读盘点、独立 namespace 最小写入、Web Console 白名单动作、cleanup 复查；结果目录为 `results/k8s/sp4-validation-20260708-023552`

已保存的 SP4 初始验证与 sched_ext 复核证据：

```text
reports/sp4/sp4_initial_validation_20260705-160156.md
reports/sp4/final_quality_gate_20260705-160156.log
reports/sp4/cmdline.before-cgroupv2-20260705-155311.txt
reports/sp4/grub.default.before-cgroupv2-20260705-155311.bak
reports/sp4/sp4_sched_ext_validation_20260705-211407.md
reports/sp4/final_quality_gate_scx_20260705-205406.log
reports/sp4/final_quality_gate_scx_workload_20260706-1214.log
reports/sp4/policy_engine_security_network_resource_scx_20260705-211329.log
reports/sp4/policy_engine_security_network_resource_repeat10_scx_20260705-211407.log
results/policy_engine/security-network-resource-20260705-211329
results/policy_engine/security-network-resource-20260705-211407
results/reports/redis-scx-smoke-20260706-092724
results/final/redis-scx-psi-probe-20260706-100857
results/final/redis-scx-compare-20260706-101505
results/final/nginx-scx-compare-20260706-101928
results/final/redis-scx-compare-20260724-tested-2541464-runs10
results/final/nginx-scx-compare-20260724-tested-2541464-runs10
results/k8s/sp4-validation-20260708-023552
```

## sched_ext/scx 复核结论

SP4 发行内核默认未启用 sched_ext：

```text
CONFIG_SCHED_CLASS_EXT is not set
/sys/kernel/sched_ext missing
```

已基于 SP4 内核源码重新编译并安装 EulerPilot 验证内核：

```text
CONFIG_SCHED_CLASS_EXT=y
CONFIG_EXT_GROUP_SCHED=y
/sys/kernel/sched_ext present
```

当前 SP4 已验证主路径为 `cgroup v2 + PSI + eBPF + Policy Engine + Web Console`，增强路径为 `sched_ext/scx available`。Web Console 在 SP4 页面中应显示为路径分工，而不是把发行内核默认限制表述为项目失败。

截至 `2026-07-06`，`scx_eulerpilot` 曾通过项目脚本构建并安装到 `/usr/local/bin/scx_eulerpilot`，启动后会额外 pin 到 EulerPilot 命名空间：

```text
/sys/fs/bpf/eulerpilot/scx_eulerpilot/v1/class_map
/sys/fs/bpf/eulerpilot/scx_eulerpilot/v1/gate_state_map
/sys/fs/bpf/eulerpilot/scx_eulerpilot/v1/stats
```

这条 `/usr/local/bin` 路径现在只作为历史环境说明。v6 封版实验必须从通过 Candidate Gate 的 `tested_code_commit` 进行 out-of-tree formal build，记录 Agent/SCX/BPF 对象 SHA256 和 `artifact_id`，并让实验脚本显式使用该 artifact 目录下的二进制。

## 已修复的 SP4 环境兼容问题

SP4 虚拟机 CPU 数量少于早期固定 cpuset 假设时，`scripts/setup_cgroup_v2.sh` 旧逻辑会固定写入 `0-1`、`2-3`、`4-7`，在单核/少核环境触发 `Numerical result out of range`。当前脚本已改为：

- 优先尝试旧的多核默认分组。
- 若写入失败，自动回退到父 cgroup 的有效 `cpuset.cpus` / `cpuset.mems`。
- 保留 `LATENCY_CPUSET`、`BATCH_CPUSET`、`BACKGROUND_CPUSET` 环境变量，方便多核实验手动指定。

## 目标

在 SP4 测试机上验证 EulerPilot 的基础能力兼容性，并补齐 sched_ext/scx 增强路径：

- Agent 编译与基础启动。
- cgroup v2 CPU/Memory/IO controller。
- BTF、BPF LSM、XDP、TC、sched_ext/scx 能力探测。
- Security、Network、Resource Control、Policy Engine 的核心集成测试。
- 自编译启用 `CONFIG_SCHED_CLASS_EXT` 的 SP4 内核，验证 `/sys/kernel/sched_ext` 与 scx 相关能力。
- 在 sched_ext 内核下复核 v3.1 主联动、质量门禁和 Web Console Evidence。

## Kubernetes 旁路验证结果

Kubernetes 验证不直接在生产 namespace 中执行。当前 `k8s-master` 集群只读盘点显示：

- context：`kubernetes-admin@kubernetes`
- Kubernetes：`v1.29.15`
- 节点：`k8s-master`、`k8s-worker1`、`k8s-worker2` 均为 `Ready`
- RuntimeClass：存在 `kata`
- 已有工具链包括 `varmor`、Kata/KataLSM、Kafka、Elasticsearch/Kibana、ebpf nodeport/service、kube-flannel、chaos-mesh 等
- 验证前已有 `ebpf-service-system/ebpf-clusterip-agent-h8lw2` 与 `ebpf-service-test/clusterip-client-cnf74` 为 `ImagePullBackOff`，属于既有状态

本次只创建并清理 `eulerpilot-sp4-validation` namespace，所有资源均带：

```text
app.kubernetes.io/part-of=eulerpilot-validation
eulerpilot.io/owner=web-console
```

Deployment/Service 最小写入验证通过，Pod 本地健康检查和 Service 健康检查均返回 `ok`。Cleanup 后两个 label selector 均返回 `No resources found`。

详细方案：`docs/sp4_k8s_validation_plan.md`

结果目录：`results/k8s/sp4-validation-20260708-023552`

## 不阻塞事项

- sched_ext/scx 不阻塞既有 SP3 证据；但 SP4 将作为后续完整能力验证平台继续推进。
- 121/122 SP3 Kubernetes/真实 runtime/真实 Pod veth 证据继续保留为历史双机对照；后续新增最终验证优先在 SP4 主验证线和独立 K8s lab 中完成。

## 检查入口

```bash
scripts/check_sp4_env.sh
```

脚本只做环境探测，不修改系统状态。若检测到 SP4，再继续执行：

```bash
make agent
./build/eulerpilot-agent --validate-config configs/agent.yaml
./build/eulerpilot-agent --list-skills
./build/eulerpilot-agent --doctor-skills --config configs/agent.yaml
sudo tests/integration/test_policy_engine_security_network_resource.sh
sudo tests/integration/test_policy_engine_security_network_resource.sh --repeat 10
sudo scripts/final_quality_gate.sh
SCX_BIN=/root/eulerpilot-artifacts/<tested_code_commit>/<artifact_id>/bin/scx_eulerpilot sudo bench/redis/run_redis_sched_ext_psi_probe.sh
SCX_BIN=/root/eulerpilot-artifacts/<tested_code_commit>/<artifact_id>/bin/scx_eulerpilot RUNS=10 sudo bench/redis/run_redis_sched_ext_compare.sh
SCX_BIN=/root/eulerpilot-artifacts/<tested_code_commit>/<artifact_id>/bin/scx_eulerpilot RUNS=10 sudo bench/nginx/run_nginx_sched_ext_compare.sh
bash bench/redis/run_redis_pressure_gradient.sh
bash bench/redis/run_static_vs_agent_compare.sh
```

上述 RUNS=10 命令只能在 Candidate Gate、formal build 和 Formal Artifact Gate 通过后执行；此前只能运行 RUNS=1 smoke 或脚本语法/语义检查。

## 结果记录

SP4 验证结果建议保存到：

```text
results/sp4-validation/<timestamp>/
```

至少包含：

- `os-release.txt`
- `uname.txt`
- `check_sp4_env.log`
- `agent_validate_config.log`
- `doctor_skills.log`
- `policy_engine_security_network_resource.log`
- `final_quality_gate.log`
- `summary.md`

## 发布链接记录

SP4 正式发布后再补入：

- openEuler 下载页：待确认
- SP4 Release Notes：待确认
- SP4 ISO/镜像源：待确认
- 对应 kernel、bpftool、clang/llvm 版本：待确认
