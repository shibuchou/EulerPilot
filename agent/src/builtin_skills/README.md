# Built-in Skill adapters

本目录保存 EulerPilot 内建 Runtime Skill 的实现。`agent/src/builtin_skills.cpp` 只负责注册入口，具体能力按 Skill 拆分到本目录，避免单文件继续膨胀。

| 文件 | 职责 |
|------|------|
| `common.hpp` | 内建 Skill 私有 helper、BPF map layout、配置解析和审计辅助函数 |
| `resource_control.cpp` | `resource_control`，封装 cgroup/scx 资源控制执行路径 |
| `psi_gate.cpp` | `psi_gate`，封装 PSI gate 状态机运行适配 |
| `network_policy.cpp` | `network_policy` 与兼容注册名 `network_policy_demo` |
| `network_qos.cpp` | `network_qos`，TC classifier + TBF QoS 适配 |
| `network_xdp.cpp` | `network_xdp`，XDP generic filter 适配 |
| `security_policy.cpp` | `security_policy` 与兼容注册名 `security_policy_demo` |
| `policy_engine.cpp` | `policy_engine`，跨 Skill 事务化联动和 rollback |
| `factories.hpp` | 各 Skill 注册函数声明 |

维护规则：

- 新增正式 Skill 优先新增独立 `.cpp`，不要直接扩展 `builtin_skills.cpp`。
- `common.hpp` 只放多个 Skill 共享的私有实现；单个 Skill 专用逻辑留在对应 `.cpp`。
- 历史 `*_demo` 注册名仅作为兼容入口，答辩和新配置默认使用正式 Skill 名。
