# tests/integration

作用：面向单个 Skill 的集成测试入口。

## 当前测试

- `test_network_policy.sh`：验证正式 `network_policy` 注册名、默认 disabled、audit 模式不挂 BPF、doctor 可通过。脚本只使用 Python 标准库，不依赖 PyYAML。

## 后续测试

- `test_security_policy.sh`
- `test_resource_control.sh`
- `test_sched_ext.sh`
