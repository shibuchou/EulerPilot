# 已知限制与演示边界

更新时间：`2026-07-08`

## 1. SP3 与 sched_ext

openEuler 24.03 LTS SP3 是主交付平台，但默认内核不保证 `sched_ext/scx` 可用。SP3 主线以 `cgroup v2` 保证稳定闭环；SP4 / OLK-6.6 / 自编译内核作为增强验证路径。

## 2. Web Console 边界

Web Console 只读取现有证据和运行白名单 demo，不生成新的性能结论，不替代 C++ Agent，不进入资源管控热路径。

## 3. Network/XDP 演示边界

默认只操作 EulerPilot lab veth 或 k3s lab Pod host veth，不操作真实业务网卡。现场如果环境不确定，优先展示已有证据，不直接运行高级 XDP/TC 按钮。

## 4. Kubernetes 高级演示

真实 Pod target 已有证据，但现场依赖 k3s、kubectl、Pod 状态和 runtime socket。推荐作为 Advanced / Optional，不作为首选主演示。

## 5. 大规模长时 benchmark

Redis/Nginx 多轮 benchmark 已冻结结果。答辩现场不建议重新跑长时 benchmark，推荐展示 frozen evidence 与 Web Console offline demo。
