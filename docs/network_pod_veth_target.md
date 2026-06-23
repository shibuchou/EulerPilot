# Network Pod/veth TargetResolver 增强说明

更新时间：`2026-06-23`

阶段：`阶段 B / Network Pod veth target 真实解析预备能力`

## 1. 目标

为后续 `network_qos` 和 `network_xdp` 从 isolated lab veth 扩展到 Kubernetes Pod veth 提供统一 target 解析入口。

本阶段完成默认安全的最小真实解析能力：

- 继续支持 `type: netdev` 的 ifname/ifindex 解析。
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
- 当前实现会短暂进入目标容器 netns 读取接口 ifindex/iflink，并立即切回原 netns；不执行任何网络修改。
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
- root + `ip` 可用时，脚本会创建临时 netns/veth，使用 fake `kubectl/crictl` 解析容器 PID，并验证 `type: k8s_pod` 能解析到 host 侧 veth ifname/ifindex。

脚本不依赖真实 Kubernetes；root 环境会创建并清理临时 netns/veth，非 root 环境跳过成功 veth 路径，只保留错误路径自测。
