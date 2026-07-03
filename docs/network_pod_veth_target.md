# Network container/Pod veth TargetResolver 增强说明

更新时间：`2026-06-23`

阶段：`阶段 B / Network container + Pod veth target 真实解析与 TC QoS 验证`

## 1. 目标

为后续 `network_qos` 和 `network_xdp` 从 isolated lab veth 扩展到容器 veth、Kubernetes Pod veth 提供统一 target 解析入口。

本阶段完成默认安全的最小真实解析能力：

- 继续支持 `type: netdev` 的 ifname/ifindex 解析。
- `type: container` 支持通过 container ID 或 runtime container name 查询容器 PID。
- `type: k8s_pod` 会先做 lab namespace 安全校验，再通过 `kubectl` 查询 Pod UID 和 container runtime ID。
- 通过 `crictl/docker/podman` 查询容器 PID，读取 `/proc/<pid>/ns/net`，并用 veth peer ifindex 反查 host 侧 veth。
- 对缺少 Kubernetes 或容器 runtime 的环境返回明确 reason code。
- Resolver 只解析目标，不创建/删除 veth，不修改 qdisc/XDP/TC；TC/XDP attach 仍由对应 Skill 显式执行。

## 2. C++ 接口

当前接口位于：

```text
agent/include/target_resolver.hpp
agent/src/target_resolver.cpp
```

新增结构：

```cpp
struct K8sPodTargetSpec {
    std::string name;
    std::string pod_namespace;
    std::string pod_name;
};

struct TargetResolverOptions {
    bool allow_non_lab_pods = false;
    bool allow_host_network_pods = false;
    bool require_runtime_socket = true;
    std::string lab_namespace = "eulerpilot-lab";
    std::string kubectl_path = "kubectl";
    std::string crictl_path = "crictl";
    std::string docker_path = "docker";
    std::string podman_path = "podman";
    std::vector<std::string> runtime_socket_paths;
};
```

新增解析入口：

```cpp
TargetIdentity resolve_container_netdev_target(
    const ContainerTargetSpec &spec,
    const TargetResolverOptions &options = TargetResolverOptions{});

TargetIdentity resolve_k8s_pod_target(
    const K8sPodTargetSpec &spec,
    const TargetResolverOptions &options = TargetResolverOptions{});

TargetIdentity resolve_k8s_pod_target(const std::string &name,
                                      const std::string &pod_namespace,
                                      const std::string &pod_name);
```

继续使用现有 netdev 入口：

```cpp
TargetIdentity resolve_netdev_target(const std::string &name,
                                     const std::string &ifname);
```

## 3. reason code

| Target | 条件 | `resolved` | `reason` |
|--------|------|------------|----------|
| `netdev` | ifname 为空、过长或包含不安全字符 | `false` | `invalid-ifname` |
| `netdev` | ifname 合法但本机不存在 | `false` | `netdev-not-found` |
| `netdev` | ifname 存在且有 ifindex | `true` | `ok` |
| `container` | container ID 为空且未提供 container name | `false` | `missing-container-id` |
| `container` | runtime name 查询不到 container ID | `false` | `container-runtime-id-not-found` / `*-container-id-not-found` |
| `container` | runtime CLI 无法解析容器 PID | `false` | `runtime-container-pid-not-found` |
| `container` | 容器使用 host network 且未显式允许 | `false` | `host-network-not-allowed` |
| `container` | 找不到 host 侧 veth | `false` | `container-veth-not-found` |
| `container` | 成功解析 host veth | `true` | `ok` |
| `k8s_pod` | namespace 为空 | `false` | `missing-pod-namespace` |
| `k8s_pod` | pod name 为空 | `false` | `missing-pod-name` |
| `k8s_pod` | 默认安全模式下 namespace 不是 `eulerpilot-lab` | `false` | `unsupported-namespace` |
| `k8s_pod` | 找不到 `kubectl` | `false` | `missing-kubectl` |
| `k8s_pod` | 找不到 containerd/cri-o/cri-dockerd 等 runtime socket | `false` | `missing-runtime` |
| `k8s_pod` | `kubectl` 查询 Pod UID 失败 | `false` | `kubectl-pod-query-failed` |
| `k8s_pod` | `kubectl` 查询 container ID 失败或格式不合法 | `false` | `kubectl-container-query-failed` / `kubectl-container-id-invalid` |
| `k8s_pod` | runtime CLI 无法解析容器 PID | `false` | `runtime-container-pid-not-found` |
| `k8s_pod` | Pod 使用 hostNetwork 且未显式允许 | `false` | `pod-host-network-not-allowed` |
| `k8s_pod` | 找不到 host 侧 veth | `false` | `pod-veth-not-found` |
| `k8s_pod` | 成功解析 host veth | `true` | `ok` |

## 4. 默认安全边界

- `k8s_pod` 默认只接受 `eulerpilot-lab` namespace；其他 namespace 必须通过 `TargetResolverOptions::allow_non_lab_pods` 显式放开。
- 当前实现通过 `nsenter -t <pid> -n ip -o link show` 读取目标 netns 接口 ifindex/iflink，不执行任何网络修改。
- `container` target 默认不要求 runtime socket 存在，但必须通过 runtime CLI 解析出真实 PID；生产环境应配合 container name/namespace allowlist 使用。
- `network_xdp` 后续仍必须拿到已解析的 ifname/ifindex 且通过 lab/allowlist 后才允许 attach，不能从 cgroup 或 Pod 名称隐式推断生产网卡。
- `network_qos` 后续接入 Pod veth 时，应先检查 `TargetIdentity::resolved` 和 `reason == "ok"`，并继续保留 lab/allowlist 约束。

## 5. 自测入口

无需 Kubernetes 的自测脚本：

```bash
tests/integration/test_target_resolver.sh
```

覆盖内容：

- `lo` netdev 解析成功。
- 不存在的 netdev 返回 `netdev-not-found`。
- 不安全 ifname 返回 `invalid-ifname`。
- 非 lab namespace 返回 `unsupported-namespace`。
- 空 PATH 下返回 `missing-kubectl`。
- fake `kubectl` 且 runtime socket 不存在时返回 `missing-runtime`。
- root + `ip` 可用时，脚本会创建临时 netns/veth，使用 fake `kubectl/crictl` 解析容器 PID，并验证 `type: container` 和 `type: k8s_pod` 都能解析到 host 侧 veth ifname/ifindex。

脚本不依赖真实 Kubernetes；root 环境会创建并清理临时 netns/veth，非 root 环境跳过成功 veth 路径，只保留错误路径自测。

## 6. 真实 k3s Pod veth QoS 证据

2026-06-30 已在 121/122 完成真实 Kubernetes lab Pod host veth QoS 验证：

- 121：`results/network_policy/real-pod-veth-qos-20260630-k3s-121-v2`
- 122：`results/network_policy/real-pod-veth-qos-20260630-k3s-122-v1`

验证链路：

```text
k3s eulerpilot-lab/eulerpilot-rc-pod
  -> kubectl 查询 Pod UID/container ID
  -> k3s crictl 查询容器 PID
  -> /proc/<pid>/ns/net + nsenter/ip 解析 host veth
  -> network_qos 在 host veth 安装 TC clsact + TBF
  -> host 到 Pod 流量命中 BPF stats
  -> Agent stop rollback 清理 qdisc
```

安全边界：

- 默认仍拒绝生产网卡前缀 `eth*`、`ens*`、`eno*`、`wlan*`、`bond*`、`br*`、`cni*`、`flannel*`。
- 只有 `type: k8s_pod/pod` 且通过 `eulerpilot-lab` namespace resolver 的目标，才允许 runtime 生成的非生产 host veth 名，例如 `veth998e0158`、`vethc59976b2`。
- `network_qos` 事件会记录 `target_ref=lab_pod`、`target_type=k8s_pod`、真实 `ifname/ifindex`、`packet_count` 和 rollback 证据。

## 7. 真实 k3s Pod veth XDP 证据

2026-07-03 已在 121/122 完成真实 Kubernetes lab Pod host veth XDP ICMP/TCP/UDP 三规则验证：

- 121：`results/network_policy/real-pod-veth-xdp-20260703-k3s-121-udp-v4`
- 122：`results/network_policy/real-pod-veth-xdp-20260703-k3s-122-udp-v4`

验证链路：

```text
k3s eulerpilot-lab/eulerpilot-rc-pod
  -> TargetResolver 解析 Pod runtime PID / netns / host veth
  -> 脚本用 nsenter 进入 Pod netns 发起 Pod-to-bridge ICMP / TCP:19092 / UDP:19093 流量
  -> network_xdp 在 host veth 挂 generic XDP
  -> XDP_DROP 命中，per-rule drop_count 增长
  -> Agent stop rollback detach XDP
  -> Pod-to-bridge 连通性恢复
```

说明：

- 测试不依赖容器镜像内置 `ping/ip/cat`，而是使用宿主机 `nsenter -t <pod-pid> -n` 进入 Pod netns 发包。
- 121 目标为 `traffic_target_ip=10.42.0.1`、`host_bridge=cni0`、`host_veth=veth998e0158`，`xdp_drop_count=13`，其中 ICMP/TCP/UDP 为 `1/4/8`。
- 122 目标为 `traffic_target_ip=10.42.0.1`、`host_bridge=cni0`、`host_veth=vethc59976b2`，`xdp_drop_count=13`，其中 ICMP/TCP/UDP 为 `1/4/8`。
- `network_policy.jsonl` 中包含 `skill=network_xdp`、`target_ref=lab_pod`、真实 `ifname/ifindex` 和 rollback `drop_icmp_real_pod/drop_tcp_real_pod/drop_udp_real_pod` per-rule 统计。
- rollback 后 `xdp_link_rollback.txt` 无 XDP attachment，`rollback_ping.txt` 证明连通性恢复。
