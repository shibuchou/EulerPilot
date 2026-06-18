# scripts

作用：项目构建、环境检查、实验汇总、图表渲染、回滚和演示辅助脚本。

## 关键脚本

- `check_env.sh`：环境检查。
- `final_quality_gate.sh`：当前 P0 质量门禁。
- `rollback.sh`：回滚入口。
- `run_agent.sh`：Agent 启动辅助。
- `render_*`：报告和图表生成脚本。
- `cleanup_network_policy_demo.sh` / `cleanup_network_qos_tc.sh` / `cleanup_security_policy_demo.sh`：当前 Network/Security 清理脚本。

## 当前完成状态

- 已能支撑当前阶段 Redis/Nginx/sched_ext 结果整理。
- v2.1 后续需要新增正式 Skill 集成测试和演示脚本：
  - `tests/integration/test_network_policy.sh`
  - `tests/integration/test_network_qos_tc.sh`
  - `tests/integration/test_security_policy.sh`
  - `tests/integration/test_resource_control.sh`
  - `tests/e2e/test_cross_agent_protection.sh`

## 维护规则

- 脚本必须默认安全，不得默认 enforce Network/Security 策略。
- 会修改系统状态的脚本必须提供清理或 rollback 路径。
- 新增脚本必须在本 README 或对应 docs 中说明用途、参数和输出位置。
