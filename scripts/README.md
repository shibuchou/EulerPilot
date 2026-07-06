# scripts

作用：项目构建、环境检查、实验汇总、图表渲染、回滚和演示辅助脚本。

## 关键脚本

- `check_env.sh`：环境检查。
- `final_quality_gate.sh`：当前 P0 质量门禁。
- `rollback.sh`：回滚入口。
- `build_scx_eulerpilot.sh`：在 SP4 sched_ext 内核源码树中构建并安装 `scx_eulerpilot`，默认输出 `/usr/local/bin/scx_eulerpilot`。
- `run_agent.sh`：Agent 启动辅助。
- `setup_cgroup_v2.sh`：初始化 `/sys/fs/cgroup/eulerpilot` 下 latency/batch/background 实验组，并尝试开启 CPU、cpuset、memory、io controller。
- `render_*`：报告和图表生成脚本。
- `collect_final_evidence.py`：根据 `configs/final_evidence_manifest.json` 生成最终证据压缩报告，输出 `reports/final_evidence_compact.md` 和 `reports/final_evidence_compact.json`。
- `cleanup_network_policy_demo.sh` / `cleanup_network_qos_tc.sh` / `cleanup_network_xdp_demo.sh` / `cleanup_security_policy_demo.sh`：当前 Network/Security 清理脚本。

## 当前完成状态

- 已能支撑当前阶段 Redis/Nginx/sched_ext 结果整理。
- `setup_cgroup_v2.sh` 已支持 Resource Control CPU+Memory+IO 闭环需要的 `cpu.max`、`memory.high`、`memory.low`、`memory.max`、`io.weight`、`io.max` 默认初始化。
- `rollback.sh` 已补充 Resource Control CPU/Mem/IO 默认恢复：`cpu.max=max`、`memory.high=max`、`memory.low=0`、`memory.max=max`、`io.weight=default 100`、`io.max=unlimited`。
- `rollback.sh` 停止 scx 调度器时已改为按进程名精确匹配，避免 `pkill -f` 误杀当前调用脚本；同时清理 `class_map/gate_state_map/stats` 的 EulerPilot 命名空间 pinned map。
- `collect_final_evidence.py --strict` 当前覆盖 28 个核心证据条目，必需证据缺失为 0、清单警告为 0。
- v2.1 后续需要新增正式 Skill 集成测试和演示脚本：
  - `tests/integration/test_network_policy.sh`
  - `tests/integration/test_network_qos_tc.sh`
  - `tests/integration/test_network_xdp.sh`
  - `tests/integration/test_security_policy.sh`
  - `tests/e2e/test_cross_agent_protection.sh`

## 维护规则

- 脚本必须默认安全，不得默认 enforce Network/Security 策略。
- 会修改系统状态的脚本必须提供清理或 rollback 路径。
- 新增脚本必须在本 README 或对应 docs 中说明用途、参数和输出位置。
