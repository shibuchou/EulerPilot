# EulerPilot 演示运行说明

更新时间：`2026-06-14`

## 1. 演示目标

1. Agent 能否真实运行
2. 是否具备正式 compare 能力
3. Skills 框架和 eBPF demo 是否可以现场演示

## 2. 建议演示顺序

### 步骤 1：项目入口
`/root/EulerPilot/README.md` + `docs/one_page_summary.md`

### 步骤 2：环境分工
- `192.168.1.121` — SP3 主交付
- `192.168.1.122` — OLK-6.6 sched_ext 验证

### 步骤 3：Skills 框架现场验证
```bash
./build/eulerpilot-agent --list-skills
./build/eulerpilot-agent --doctor-skills --config configs/agent.yaml
```
预期输出 4 项：resource_control, psi_gate, network_policy_demo, security_policy_demo

### 步骤 4：network_policy_demo 现场演示
- 启动 `python3 -m http.server 18080 --bind 127.0.0.1`
- 临时启用 network_policy_demo，启动 Agent
- demo-net cgroup 内 curl 被 deny（000），cgroup 外正常（200）
- Agent 退出后恢复

### 步骤 5：security_policy_demo 现场演示
- 临时启用 security_policy_demo，启动 Agent
- `cat demo/security_policy_demo/secret.txt` -> Operation not permitted
- Agent 退出后恢复

### 步骤 6：Redis 候选结果
`results/final/redis-scx-compare-20260612-191543`
重点看 run_manifest.json / compare_summary_avg.csv / report.md

### 步骤 7：Nginx 候选结果
`results/final/nginx-scx-compare-20260612-194018`

### 步骤 8：图表
`reports/final_figures/`（7 张 SVG）

## 3. 演示口径
> EulerPilot 已完成 SP3 主闭环到 OLK-6.6 sched_ext 正式 compare 的工程收口，Skills 框架和 network/security demo 证明 Agent 可扩展，项目已形成 Redis/Nginx 双业务线 RUNS=5 候选结果目录。

## 4. 演示边界
强调：正式 compare 能力和系统创新闭环已成立，sched_ext 结果需按 workload 谨慎解释。
不强调：sched_ext 全面优于默认调度器、某一组参数绝对最优。
