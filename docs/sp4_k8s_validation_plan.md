# SP4 / Kubernetes 旁路验证方案

更新时间：`2026-07-20`

## 目标

以后以 `192.168.1.123:/root/EulerPilot` 的 openEuler 24.03 LTS SP4 仓库作为核心验证和最终交付验证仓库。Kubernetes master 只作为外部集群验证面，所有验证必须旁路、隔离、可复现、可清理，不影响已有业务和系统组件。

## 基线角色

- SP4 主验证仓库：`openEuler-2403-LTS-SP4:/root/EulerPilot`
- SP3 历史仓库：`EulerPilot-openEuler:/root/EulerPilot`
- Kubernetes master：按本地 SSH config 的 `k8s-master` 连接

SP4 上当前可用能力：`cgroup v2`、PSI、BTF、BPF LSM、TC/XDP、`sched_ext/scx`。Kubernetes master 当前提供集群控制面，但 SP4 主机本身未安装 `kubectl`，因此真实集群验证优先通过 `k8s-master` 执行，结果回收到 SP4 主验证仓库。

## 执行分级

### 1. 只读检查

只读采集以下内容，不创建任何资源：

- `kubectl config current-context`
- `kubectl get nodes -o wide`
- `kubectl get ns --show-labels`
- `kubectl get pods -A -o wide`
- `kubectl get runtimeclass`
- `kubectl get crd`
- `kubectl get all -A -l app.kubernetes.io/part-of=eulerpilot-validation`
- `kubectl get all -A -l eulerpilot.io/owner=web-console`

检查时必须记录既有异常，避免把历史问题误归因到 EulerPilot。

### 2. 最小写入验证

仅创建独立 namespace：

```text
eulerpilot-sp4-validation
```

所有资源必须带：

```text
app.kubernetes.io/part-of=eulerpilot-validation
eulerpilot.io/owner=web-console
```

最小 workload 使用低资源限制：

```text
requests: cpu=10m, memory=16Mi
limits:   cpu=50m, memory=64Mi
```

默认只创建一个 `Deployment` 和一个 `ClusterIP Service`，验证 Pod 本地健康检查和 Service 访问，不修改已有 namespace、DaemonSet、RuntimeClass、CRD 或生产网络。

### 3. 受控 live 验证

优先选择一条代表性链路：

- 专用 K8s lab smoke：独立 namespace + 专用 Pod + Service + cleanup。
- 或 SP4 本机 `policy_engine_lab`：只作用于 EulerPilot lab cgroup/veth/qdisc。

不要一次性运行全部 Network/Security/Resource 集成测试。涉及 qdisc、XDP、LSM、cgroup 写入的测试必须先确认目标是 EulerPilot lab 资源。

### 4. 清理与复查

验证完成后删除本次 namespace，并复查：

```bash
kubectl get all -A -l app.kubernetes.io/part-of=eulerpilot-validation
kubectl get all -A -l eulerpilot.io/owner=web-console
kubectl get nodes -o wide
kubectl -n kube-system get pods -o wide
kubectl -n varmor get pods -o wide
kubectl get pods -A -o wide
```

清理结果必须进入 `results/k8s/sp4-validation-<timestamp>/`。

## 结果目录

本阶段结果保存到：

```text
results/k8s/sp4-validation-20260708-023552/
```

关键文件：

- `k8s-master-raw/`：Kubernetes master 原始验证输出。
- `web_console/`：SP4 Web Console API 与白名单动作输出。
- `final_quality_gate.log`：SP4 主验证仓库最终质量门禁。
- `report.md`：本次验证摘要。

## 边界

- 不修改 `agent/`、`bpf/`、`sched/` 主干。
- 不改变 C++ Agent 和已有实验脚本语义。
- Web Console 保持旁路展示控制台，只读取现有 evidence 并触发白名单动作。
- 未经用户确认不主动 push。
