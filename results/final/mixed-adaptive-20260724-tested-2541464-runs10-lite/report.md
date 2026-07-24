# Mixed-Adaptive 完整闭环证据

- 结果目录：`/root/eulerpilot-runs/2541464552aa763522a8496a5082a514a843a179/formal-20260723-153923/mixed-adaptive-runs10-lite`
- 轮数：`10`
- stress workers：`4`

## 链路

压力出现 -> PSI/wait 变化 -> Gate 进入 ACTIVE -> class_map / gate_state 更新 -> 压力消失 -> recovery 阶段确认恢复与 rollback。

## 平均结果

| phase | runs | GET RPS avg | GET P99 ms avg | ACTIVE 次数 | COOLDOWN 次数 | recovery evidence | switch latency ms | scheduler update evidence |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| quiet_pre | 10 | 36213.814 | 0.409 | 0 | 0 | 10 |  | 10 |
| pressure_active | 10 | 6385.350 | 3.465 | 10 | 10 | 10 | 1116.818 | 10 |
| recovery | 10 | 36136.128 | 0.421 | 0 | 0 | 10 |  | 10 |

## 结论边界

`pressure_active` 阶段包含额外 Redis PSI probe，目的在于稳定触发 PSI gate 和调度路径，不作为净性能提升结论；性能收益仍需结合无额外 probe 的 Redis/Nginx RUNS=5 对照解释。
