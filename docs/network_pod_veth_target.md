# Network Pod/veth TargetResolver 增强说明

更新时间：`2026-06-20`

阶段：`阶段 B / Network Pod veth target 预备能力`

## 1. 目标

为后续 `network_qos` 和 `network_xdp` 从 isolated lab veth 扩展到 Kubernetes Pod veth 提供统一 target 解析入口。

本阶段只做默认安全的最小能力：

- 继续支持 `type: netdev` 的 ifname/ifindex 解析。
- 新增 `type: k8s_pod` 的诊断型解析入口。
- 对缺少 Kubernetes 或容器 runtime 的环境返回明确 reason code。
- 不执行 `kubectl` 查询，不进入 netns，不创建/删除 veth，不修改 qdisc/XDP/TC。

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
    std::string lab_namespace = "eulerpilot-lab";
    std::string kubectl_path = "kubectl";
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
| `k8s_pod` | 环境具备但 Pod veth 映射尚未实现 | `false` | `unsupported-pod-veth-resolution` |

## 4. 默认安全边界

- `k8s_pod` 默认只接受 `eulerpilot-lab` namespace；其他 namespace 必须通过 `TargetResolverOptions::allow_non_lab_pods` 显式放开。
- 当前实现只做前置诊断，不读取 Pod 网络命名空间，不解析容器 PID，不触碰 TC/XDP attachment。
- `network_xdp` 后续仍必须拿到显式 ifname/ifindex 后才允许 attach，不能从 cgroup 或 Pod 名称隐式推断生产网卡。
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

脚本只编译并运行 `target_resolver.cpp` 的 C++ selftest，不依赖 Kubernetes，不需要 root，不修改系统网络。
