# scripts

作用：项目构建、环境检查、实验汇总、图表渲染、回滚和演示辅助脚本。

## 关键脚本

- `check_env.sh`：环境检查。
- `v6_preflight_readiness.sh`：v6 候选提交前开发检查入口，记录 dirty 状态、diff hash、29 项缩短质量门、Web Console 测试、Python 静态检查和文档旧口径扫描；它不是 Candidate-bound Gate。
- `final_quality_gate.sh`：当前 P0 质量门禁。
- `rollback.sh`：回滚入口。
- `build_scx_eulerpilot.sh`：构建 `scx_eulerpilot`。历史环境曾安装到 `/usr/local/bin/scx_eulerpilot`；v6 正式实验必须使用 formal artifact 目录中的二进制。
- `run_agent.sh`：Agent 启动辅助。
- `setup_cgroup_v2.sh`：初始化 `/sys/fs/cgroup/eulerpilot` 下 latency/batch/background 实验组，并尝试开启 CPU、memory、io controller；cpuset 默认关闭，只有设置 `EULERPILOT_ENABLE_CPUSET=1` 时才作为实验路径启用。
- `render_*`：报告和图表生成脚本。
- `collect_final_evidence.py`：根据 `configs/final_evidence_manifest.json` 生成最终证据压缩报告，输出 `reports/final_evidence_compact.md` 和 `reports/final_evidence_compact.json`。
- `cleanup_network_policy_demo.sh` / `cleanup_network_qos_tc.sh` / `cleanup_network_xdp_demo.sh` / `cleanup_security_policy_demo.sh`：当前 Network/Security 清理脚本。

## 当前完成状态

- 已能支撑当前阶段 Redis/Nginx/sched_ext 历史结果整理和 v6 formal artifact 收口。
- `setup_cgroup_v2.sh` 已支持 Resource Control CPU+Memory+IO 闭环需要的 `cpu.max`、`memory.high`、`memory.low`、`memory.max`、`io.weight`、`io.max` 默认初始化。
- `rollback.sh` 已补充 Resource Control CPU/Mem/IO 默认恢复：`cpu.max=max`、`memory.high=max`、`memory.low=0`、`memory.max=max`、`io.weight=default 100`、`io.max=unlimited`。
- `rollback.sh` 不再使用 `pkill` 兜底停止 scx 调度器；Agent 侧通过 ScxExecutor 记录的 PID/starttime/executable identity 停止自有实例，安全 detach 失败时保留 pinned map，避免误删外部调度器状态。
- `collect_final_evidence.py` 当前覆盖 41 个核心证据条目，必需证据缺失为 0、预期警告为 8；旧 SP4 性能结果通过 `evidence/evidence_status_overrides.json` 降级，最终 release 使用 `--validate-release`。
- `v6_preflight_readiness.sh` 会把结果写入 `reports/gates/v6-preflight-<timestamp>/`。该目录用于 pre-candidate 开发审计，不得作为正式 tested commit、formal artifact 或 release gate 证据。
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
