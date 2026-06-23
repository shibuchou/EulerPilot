# Network TargetResolver Pod Veth 20260623-103729

运行环境：`192.168.1.122:/root/EulerPilot`，openEuler 24.03 LTS SP3。

运行命令：

```bash
cd /root/EulerPilot
make agent
bash tests/integration/test_target_resolver.sh
```

结论：`TargetResolver` 在第二台 SP3 验证机上同样通过 Pod host veth 解析预备验证。测试使用临时 netns/veth 和 fake `kubectl/crictl`，覆盖 `k8s_pod -> Pod UID/container ID -> runtime PID -> netns -> host veth ifname/ifindex` 路径，不依赖真实 Kubernetes 集群。

关键证据：

- `test.log`：包含 `PASS: target resolver netdev and k8s_pod runtime/veth paths`。
- 覆盖 `netdev` 成功/失败路径、非 lab namespace 拒绝、缺 `kubectl`、缺 runtime socket，以及 fake runtime 下的 host veth 解析成功路径。

边界：本阶段只完成安全解析和 `network_qos/network_xdp` 的 Pod target 接入预备；真实 Kubernetes lab Pod 上的 TC/XDP attach 演示仍是下一阶段任务。
