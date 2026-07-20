# Mixed-Adaptive 完整闭环证据

- 结果目录：`/root/EulerPilot/results/final/mixed-adaptive-20260720-170840`
- 轮数：`3`
- stress workers：`2`

## 链路

压力出现 -> PSI/wait 变化 -> Gate 进入 ACTIVE -> class_map / gate_state 更新 -> 压力消失 -> recovery 阶段确认恢复与 rollback。

## 平均结果

| phase | runs | GET RPS avg | GET P99 ms avg | ACTIVE 次数 | COOLDOWN 次数 | recovery evidence | switch latency ms | scheduler update evidence |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| quiet_pre | 3 | 36590.987 | 0.444 | 0 | 0 | 3 |  | 3 |
| pressure_active | 3 | 6639.153 | 3.431 | 3 | 0 | 0 | 946.592 | 3 |
| recovery | 3 | 35763.440 | 0.404 | 0 | 0 | 3 |  | 3 |

## 结论边界

`pressure_active` 阶段包含额外 Redis PSI probe，目的在于稳定触发 PSI gate 和调度路径，不作为净性能提升结论；性能收益仍需结合无额外 probe 的 Redis/Nginx RUNS=5 对照解释。
