# Stage G Benchmark 与冻结材料

更新时间：`2026-07-06`

## 结论

Stage G 已进入完成状态。当前提交材料不再新增 Benchmark 主结论，而是冻结已有 Redis/Nginx、Network QoS、Security、Resource Control、Policy Engine 与 SP4 sched_ext 复核证据，统一由 final evidence manifest 和 compact 报告索引。

## 冻结范围

| 类别 | 冻结证据 |
|------|----------|
| Redis 主结果 | `results/final/redis-scx-compare-20260612-191543` |
| Nginx 主结果 | `results/final/nginx-scx-compare-20260612-194018` |
| SP4 Redis 复核 | `results/final/redis-scx-compare-20260706-115029` |
| SP4 Nginx 复核 | `results/final/nginx-scx-compare-20260706-120547` |
| SP4 PSI ACTIVE | `results/final/redis-scx-psi-probe-20260706-100857` |
| Network QoS Benchmark | `results/network_policy/qos-rate-20260620-181708`、`results/network_policy/qos-rate-20260620-181755` |
| Resource Control Benchmark | `results/resource_control/redis-quota-*`、`results/resource_control/nginx-quota-*`、`results/resource_control/mixed-*` |
| Policy Engine 演示压测 | `results/policy_engine/security-network-resource-20260706-145547` |
| 证据入口 | `configs/final_evidence_manifest.json`、`reports/final_evidence_compact.md/json` |

## 使用规则

- 提交材料引用 `reports/final_evidence_compact.md` 作为总入口。
- Redis/Nginx 主结论以 121/122 既有正式候选目录为主，SP4 RUNS=3 作为平台复核证据。
- 新增现场演示日志只作为彩排记录，不自动扩展 final evidence compact 的 35 条冻结范围；SP4/K8s 旁路验证已作为最终收口证据纳入当前 manifest。
- Benchmark 结论必须说明环境、目标 workload、对照组和边界，不能把单 workload profile 直接泛化到所有场景。

## 后续只允许的改动

- 修正文档错别字或路径错误。
- 补充现场截图、录屏或演示日志。
- 若新增 Benchmark，必须另建结果目录并在报告中标注为“追加验证”，不得覆盖冻结结论。
