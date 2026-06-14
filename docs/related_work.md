# 相关工作与差异化定位

## 需要参考但避免同质化的方向

- sched-ext/scx：sched_ext 调度器和工具集合，是 scx_eulerpilot 的核心参考。
- scx-c-examples：C 语言 scx 原型参考。
- SchedCP / Towards Agentic OS：Agent + sched_ext 方向相近，需要避免简单复刻。
- Unfair by design：混合数据库 workload 下前台任务优先、后台任务 idle-only 的重要参考。
- ghOSt / Ekiben：用户态调度控制和快速迭代思想参考。
- LMP：eBPF 工具组织和可视化参考。
- Linux cgroup v2：cgroup_control_skill 的官方机制参考。

## EulerPilot 差异化

EulerPilot 不主打 LLM 自动生成调度器，而是面向 openEuler 赛题环境构建可解释、可回滚、可扩展的资源管控 Agent：

- workload score 可解释。
- scx/cgroup 双执行路径。
- Skills 插件框架服务真实执行能力。
- benchmark 和报告强调可复现。
