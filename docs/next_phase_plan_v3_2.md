# EulerPilot v3.2 执行方案：真实 runtime / Kubernetes target 转 pass

更新时间：`2026-06-30`

## Summary

v3.2 的主线不是继续堆新的 LSM hook 或新的调度策略，而是把 v3.1 保留下来的最大 blocked debt 转为 pass：真实 container runtime、Kubernetes lab Pod 和 Pod veth target。

目标链路：

```text
真实 container / Kubernetes Pod
  -> TargetResolver 解析 cgroup 与 host veth
  -> resource_control 写 cpu.max / memory.high / io.*
  -> network_qos 或 network_xdp 作用于 Pod host veth
  -> policy_engine 可复用同一 target_ref 触发联动
  -> AuditBus + ActionJournal + rollback
```

v3.2 不以 SP4 为阻塞条件。SP4 发布后只作为追加平台验证；当前主证据仍以 121/122 的 openEuler 24.03 LTS SP3 与 OLK 6.6 验证为基础。

## 当前事实

2026-06-30 已刷新只读诊断：

| 机器 | Kernel | Runtime/K8s 状态 | 结果目录 |
|------|--------|------------------|----------|
| 121 | `6.6.0-132.0.0.111.oe2403sp3.x86_64` | `missing-container-runtime-and-kubernetes-lab` | `results/resource_control/runtime-readiness-20260630-1020-121` |
| 122 | `6.6.0-olk66-scx` | `missing-container-runtime-and-kubernetes-lab` | `results/resource_control/runtime-readiness-20260630-1020-122` |

真实 runtime target 当前证据：

- 121：`results/resource_control/real-runtime-target-20260630-1020-121`，`reason=missing-docker-podman-or-isula`
- 122：`results/resource_control/real-runtime-target-20260630-1020-122`，`reason=missing-docker-podman-or-isula`

真实 Pod target 当前证据：

- 121：`results/resource_control/real-pod-target-20260630-1020-121`，`reason=missing-kubectl`
- 122：`results/resource_control/real-pod-target-20260630-1020-122`，`reason=missing-kubectl`

SP4 环境探测：

- 121：`results/resource_control/sp4-env-20260630-101422-121.log`，`sp4_detected=no`
- 122：`results/resource_control/sp4-env-20260630-101422-122.log`，`sp4_detected=no`

## Key Changes

### 1. openEuler runtime 兼容性

openEuler 场景不能只假设 docker/podman。v3.2 第一项已开始补 `iSulad/isula`：

- `TargetResolver` 增加 `isula_path`。
- `runtime=auto` 时按 `crictl -> docker -> podman -> isula` 尝试解析 container ID。
- container PID 解析增加 `isula inspect -f '{{.State.Pid}}'`。
- runtime socket 候选增加 `/var/run/isulad.sock` 与 `/run/isulad.sock`。
- readiness 脚本记录 `isula` 命令、`isulad` 服务、isulad socket 和 `isula ps -a`。
- 真实 runtime target 脚本支持 `EULERPILOT_RUNTIME_KIND=isula`。

### 2. 真实 container target pass

优先目标：在 121 或 122 至少一台机器上把 `test_resource_control_real_runtime_target.sh` 从 blocked 转 pass。

环境前置：

```text
docker / podman / isula 任一可用
本地存在 busybox:latest，或显式设置 EULERPILOT_RUNTIME_IMAGE
如需拉镜像，必须显式设置 EULERPILOT_ALLOW_IMAGE_PULL=1
```

验收入口：

```bash
sudo tests/integration/test_resource_control_runtime_readiness.sh
sudo tests/integration/test_resource_control_real_runtime_target.sh
```

必须证明：

- `type: container + container_name + runtime` 能解析到真实 cgroup。
- 目标 cgroup 写入 `cpu.max=10000 100000` 与 `memory.high=1048576`。
- 非目标 cgroup 不被误改。
- Agent stop 后恢复旧值。
- `resource_control.jsonl` 与 `ActionJournal` 均有 applied/restored 证据。

### 3. 真实 Kubernetes Pod target pass

目标：在 `eulerpilot-lab` namespace 中准备 demo Pod，并把 Pod cgroup target 从 blocked 转 pass。

环境前置：

```text
kubectl 可用
存在 eulerpilot-lab namespace
存在 eulerpilot-rc-pod demo Pod
如需脚本创建，显式设置 EULERPILOT_ALLOW_K8S_CREATE=1
```

验收入口：

```bash
sudo tests/integration/test_resource_control_real_pod_target.sh
```

必须证明：

- `type: k8s_pod + namespace + pod_name` 能解析 Pod UID 与真实 cgroup。
- 目标 Pod cgroup 写入并恢复 `cpu.max/memory.high`。
- 默认只允许 `eulerpilot-lab`，非 lab namespace 仍然拒绝。

### 4. Network Pod veth pass

Resource Control real Pod pass 后，再推进 Network 真实 Pod veth：

```text
k8s_pod target
  -> TargetResolver 解析 runtime PID / netns / host veth
  -> network_qos tc/tbf on host veth
  -> network_xdp attach on host veth
  -> rollback 清理 qdisc / XDP
```

验收重点：

- 只作用于 `eulerpilot-lab` Pod 的 host veth。
- 默认禁止生产网卡与非 lab namespace。
- TC/XDP attach 后有 qdisc/map 命中证据。
- cleanup 后无 qdisc、XDP、pinned map 残留。

### 5. Policy Engine 真实 target 联动

当 Resource + Network 真实 target 均 pass 后，将 v3.1 第二条联动从 lab cgroup/veth 扩展为真实 runtime/Pod target：

```text
security_policy anomaly
  -> policy_engine
  -> resource_control real container/Pod cgroup
  -> network_qos Pod host veth
  -> transaction_id
  -> rollback
```

## Test Plan

基础回归：

```bash
make agent
./build/eulerpilot-agent --validate-config configs/agent.yaml
./build/eulerpilot-agent --validate-config configs/policy_engine_security_network_resource.yaml
./build/eulerpilot-agent --doctor-skills --config configs/agent.yaml
tests/integration/test_target_resolver.sh
tests/integration/test_resource_control_runtime_target.sh
```

v3.2 环境入口：

```bash
sudo tests/integration/test_resource_control_runtime_readiness.sh
sudo tests/integration/test_resource_control_real_runtime_target.sh
sudo tests/integration/test_resource_control_real_pod_target.sh
```

最终回归：

```bash
tests/integration/test_policy_engine_security_network_resource.sh
scripts/final_quality_gate.sh
```

## Success Criteria

- 至少一台 openEuler 机器完成真实 container target pass。
- 至少一台 openEuler 机器完成真实 Kubernetes Pod target pass；如果没有 K8s 环境，必须保留 blocked 证据和明确 next_action。
- `TargetResolver` 支持 docker、podman、crictl、containerd/ctr 场景外，还覆盖 openEuler 常见 iSulad/isula。
- Network Pod veth 演示不操作生产网卡，只操作 lab Pod host veth。
- Policy Engine 的真实 target 联动仍保留 transaction_id、ActionJournal 和 rollback。
- 121/122/GitHub/本地同步后，更新 `docs/progress_status.md`、`docs/final_evidence_index.md` 和对应结果目录。

## Risks

- 安装或启动 runtime 是系统级动作，v3.2 不默认自动执行，需要用户明确允许。
- 镜像拉取依赖网络，默认不拉取；优先使用本地已有镜像。
- Kubernetes 环境可能不可用，必须保持 blocked 结果可解释，不能把环境缺失写成功能失败。
- Pod host veth attach 需要严格白名单，避免影响 SSH、管理网卡、CNI 主链路。