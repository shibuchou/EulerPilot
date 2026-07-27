# 证据摘要

更新时间：`2026-07-26`

最终证据以白名单清单、状态覆盖和 release validation 为准：

```bash
python3 scripts/collect_final_evidence.py
```

生成文件：

- `reports/final_evidence_compact.md`
- `reports/final_evidence_compact.json`

当前核心状态：

```text
entries=41
missing_required=0
warnings=8
```

8 条 warning 来自旧 SP4 RUNS=10 结果降级：Redis/Nginx、pressure gradient、static-vs-agent 和 Agent overhead 为 provisional/historical；throughput-first 与 mixed-adaptive 为 invalid historical。正式封版需在 formal artifact 重跑后执行 `python3 scripts/collect_final_evidence.py --validate-release`。

## 重点证据

| 类别 | 路径 |
|------|------|
| 质量门禁 | `reports/final_quality_gate_20260720-stage3-performance.log` |
| Redis/Nginx SP4 RUNS=10 historical/provisional | `results/final/redis-scx-compare-20260724-tested-2541464-runs10`、`results/final/nginx-scx-compare-20260724-tested-2541464-runs10` |
| Redis 压力梯度 historical/provisional | `results/final/redis-pressure-gradient-20260724-tested-2541464-runs3` |
| Redis 静态调参 vs Agent 动态调控 historical/provisional | `results/final/redis-static-vs-agent-20260724-tested-2541464-runs10` |
| Throughput-first / Mixed-Adaptive invalid historical；Agent overhead provisional | `results/final/throughput-first-20260724-tested-2541464-runs10`、`results/final/mixed-adaptive-20260724-tested-2541464-runs10-lite`、`results/final/agent-overhead-20260724-tested-2541464-runs10` |
| Policy Engine 跨 Skill 联动 | `results/policy_engine/security-network-resource-20260705-211407` |
| Network | `results/network_policy/` |
| Security | `results/security_policy/` |
| Resource Control | `results/resource_control/` |
| Web Console / K8s 旁路验证 | `web_console/`、`results/k8s/sp4-validation-20260708-023552` |

## 评分项映射

| 评分项 | 证据 |
|--------|------|
| 创新性 | Skills 框架、Policy Engine、跨 Skill 联动、PSI/自适应阈值、sched_ext/scx 增强路径 |
| 功能完整性 | CPU Scheduling、Resource Control、Network Policy/QoS/XDP、Security Policy/LSM、Policy Engine |
| 性能提升 | 当前只保留 historical/provisional 趋势证据；正式收益等待修复 baseline 和 formal artifact 后重跑 |
| 代码质量 | C++ 单元测试、Web Console 测试、final_quality_gate、strict evidence、残留清理 |
| 演示效果 | Web Console、`demo_all_final.sh`、Policy Engine live lab、cleanup、final evidence compact |

## 边界说明

- `sched_ext/scx` 不表述为 SP4 发行默认内核直接可用；当前口径是“SP4 发行环境适配 + SP4 官方源码自编译启用内核复核”。
- Redis / latency-sensitive 混布场景收益更明确；Nginx 和部分 scx 模式体现 workload 相关边界，不写成所有 workload 永远提升。
- 仓库当前未包含正式演示视频文件；录制脚本见 `docs/demo_video_recording_script.md`。
