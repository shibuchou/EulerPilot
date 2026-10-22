# 验证层级

| 层级 | 入口 | 状态 |
|---|---|---|
| 设计契约 | `scripts/validate_design.py` | 已通过（3 schema、正/负样例） |
| 单元 | `tests/unit/test_main.cpp` | 已通过（journal、flock、幂等、generation、rollback、reconcile、策略校验、target 解析、慢 apply 跨 deadline 补偿） |
| 集成 | `tests/integration.sh` | 已通过（真实 daemon+CLI、重启、第二实例拒绝、合成崩溃 reconcile） |
| 真实内核集成 | `tests/integration_sched.sh` | 已通过（SP3 真实 sched_ext attach/detach、cfg 参数写入回读、SIGKILL 后退回 fair、孤儿回收；无 root/loader 时 SKIP） |
| FD 传递 link | `tests/integration_fd.sh` | 已通过（`sched` 模式：loader 退出、执行服务持有 bpf fd、崩溃 FD 关闭 detach；无 root/loader 时 SKIP） |
| 租约过期回退 | `tests/integration_lease.sh` | 已通过（apply TTL=1s，租约到期后自动 detach、清 pin、status degraded/last_fallback；无 root/loader 时 SKIP） |
| 策略 TTL 回退 | `tests/integration_ttl.sh` | 已通过（policy_ttl_ms=10s，到期自动 detach、清 pin、last_fallback=policy TTL；无 root/loader 时 SKIP） |
| FeatureWindow | `tests/integration_fd.sh` | 已通过（`feature.snapshot` samples≥1、load1 valid、run_delay invalid 带 missing） |
| 规则路由 | `tests/integration_fd.sh` | 已通过（`route.decide` 返回 expert/degraded；2 样本时保守 safe-basic） |
| SLO 门控 | `tests/integration_fd.sh` | 已通过（2 样本时 `slo.evaluate` 拒绝 FEATURES_INSUFFICIENT） |
| 非人工来源 SLO 否决 | `tests/integration_fd.sh` | 已通过（`llm-proposal` apply 返回 SLO_DENIED，状态不变） |
| A/B/C liveness | `experiments/abc_nostall.sh` | 已通过（fair/basic/class 三臂 utime 均推进、状态干净；非性能比较） |
| WAL 故障 | unit（Journal fault injection） | 已通过（PREPARED/VERIFYING/ROLLBACK_PREPARED/ROLLED_BACK 失败路径；无假 ROLLED_BACK） |\n| FD/helper 故障 | unit + integration_fd | 已通过（link fd CLOEXEC；整轮刷新 204ms、EOF 208ms；正常清理与外来 pin 保留/恢复） |
| 一键验收 | `scripts/accept_sp3.sh` | 旧版SP3日志通过；新版要求6必测项无SKIP及严格收尾断言，离线回归通过，尚待SP3重跑 |
| class_map BACKGROUND | `tests/integration_fd.sh` | 已通过（pid 直接写 + cgroup.procs 展开；BG/EXTERN，class-get 验证） |
| 调度 loader 构建 | `sched/loader.c` → `ep-sched-loader` | 已通过（需内核树静态 libbpf ≥1.3） |
| 性能 | 按 `docs/EXPERIMENT_PROTOCOL.md` | 未开始 |
| 故障恢复 | 目前只有控制面故障 | 部分 |

`ctest` 同时运行单元与集成测试；集成测试需要 Python 3，会在临时目录启动并杀掉真实守护进程。
