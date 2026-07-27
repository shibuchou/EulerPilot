# Stage G Benchmark 与冻结材料

更新时间：`2026-07-26`

## 结论

Stage G 在 v6 复审后撤回“正式冻结完成”状态。当前 Redis/Nginx、pressure gradient、static-vs-Agent、Agent overhead 保留为 historical/provisional evidence；throughput-first 与 mixed-adaptive 保留为 invalid historical。正式 Benchmark 冻结必须等待 Candidate Gate、formal artifact、Formal Artifact Gate 和修正 baseline 后重新运行。

## 冻结范围

| 类别 | 冻结证据 |
|------|----------|
| Redis 主结果 | `results/final/redis-scx-compare-20260612-191543` |
| Nginx 主结果 | `results/final/nginx-scx-compare-20260612-194018` |
| SP4 Redis historical/provisional | `results/final/redis-scx-compare-20260724-tested-2541464-runs10` |
| SP4 Nginx historical/provisional | `results/final/nginx-scx-compare-20260724-tested-2541464-runs10` |
| SP4 PSI ACTIVE | `results/final/redis-scx-psi-probe-20260706-100857` |
| Network QoS Benchmark | `results/network_policy/qos-rate-20260620-181708`、`results/network_policy/qos-rate-20260620-181755` |
| Resource Control Benchmark | `results/resource_control/redis-quota-*`、`results/resource_control/nginx-quota-*`、`results/resource_control/mixed-*` |
| Policy Engine 演示压测 | `results/policy_engine/security-network-resource-20260706-145547` |
| 证据入口 | `configs/final_evidence_manifest.json`、`reports/final_evidence_compact.md/json` |

## 使用规则

- 提交材料引用 `reports/final_evidence_compact.md` 作为总入口。
- Redis/Nginx 主结论不能再直接引用 20260724 historical RUNS=10 目录；121/122 既有 RUNS=5 目录只作为历史候选和对照证据。
- 新增现场演示日志只作为彩排记录，不自动扩展当前 final evidence compact 范围；SP4/K8s 旁路验证可作为功能证据，Redis pressure gradient 和 static-vs-agent 当前只作为 provisional historical。
- Benchmark 结论必须说明环境、目标 workload、对照组和边界，不能把单 workload profile 直接泛化到所有场景。

## 后续只允许的改动

- 修正文档错别字或路径错误。
- 补充现场截图、录屏或演示日志。
- 若新增 Benchmark，必须另建结果目录并在报告中标注 tested commit、formal artifact、随机种子和 suite validation；不得覆盖历史结果。
