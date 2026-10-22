# 赛题与验收基准

记录日期：2026-10-21。以下以用户本次提供的赛题文案为需求基准；外部页面核验情况另见 `research/sources.md`。不从旧赛题或旧项目推导新规则。

## 比赛信息

- 命名日期：2026-10-08。
- 比赛：2026 年全国大学生计算机系统能力大赛—操作系统设计赛—中西部与华南区域赛—功能挑战赛道。
- 中文赛题：面向 openEuler 的自适应资源管控 Agent。
- 英文赛题：Adaptive Resource Management Agent for openEuler。
- 难度 B；维护单位武汉大学；维护人蔡朝晖；联系邮箱 `zhcai@whu.edu.cn`。
- **交付要求：在 openEuler 24.03-LTS-SP3 正常编译、运行和测试。** 鼓励其他发行版兼容验证。
- 截止时间、团队规模、指定机器、是否允许自定义内核/VM：用户尚未提供，不擅自补全。

核心目标是用户态 Agent 感知 workload，按负载特征动态选择或生成资源策略，重点通过 Linux sched_ext/scx 改善 CPU 调度。扩展可使用 eBPF hook 连接网络、安全、cgroup。

“选择或生成”允许先完成可靠的策略选择；DSL 生成是增强能力。“标准化 Tool/Skills”在本项目解释为版本化 manifest、输入输出 schema、统一生命周期和可测试契约；文案没有强制 MCP，也没有要求必须使用 LLM。

## 需求和验收映射

| ID | 要求/评分 | v2 实现规划 | 必须提交的证据 |
|---|---|---|---|
| R1 | 可扩展用户态 Agent、Tool/Skills | Agent + manifest/schema + 执行服务 + CLI | 新增示例 Skill、协议版本测试、统一错误/生命周期 |
| R2 | workload 感知与策略调整 | 调度指标、PSI、可选 PMU、规则分类/路由 | 特征有效性、身份绑定、相位变化与策略版本时间线 |
| R3 | scx 实际运行闭环 | 移植 SchedPilot 数据面、唯一写者、反馈验证 | SP3 上真实 attach、调度计数和 workload 存活 |
| R4 | 性能提升及复现 | 统一实验协议、同构环境与资源预算 | SP3 正式原始数据、配对统计、失败记录、构建哈希 |
| R5 | eBPF 扩展能力 | cgroup Skill + 一条 eBPF 事件触发的可恢复链路；network/security 接口 | 至少一条真实 hook→决策→动作→恢复，不能只有空接口 |
| R6 | 创新性 30 | 受 SLO/后台进度约束的策略选择、能力退化、可信发布 | 对固定专家/已有方法的差异与消融；复用思想如实归属 |
| R7 | 功能完整性 25 | R1/R2/R3/R5 | 功能矩阵及负路径 |
| R8 | 性能提升 25 | R4 | 默认 fair 对照、强基线和 quiet 回归 |
| R9 | 代码质量 10 | 模块边界、许可、构建与测试 | 审查、依赖锁定、单元/集成测试 |
| R10 | 演示效果 10 | CLI、机器可读状态、现场故障恢复 | 可重复脚本、实时/历史标记、验收报告 |

参考链接与文献名称：

- [赛题所附微信材料](https://mp.weixin.qq.com/s/ewC8gTMRvm2NO6ywoproAA)。
- Towards Agentic OS: An LLM Agent Framework for Linux Schedulers。
- Mixture-of-Schedulers: An Adaptive Scheduling Agent as a Learned Router for Expert Policies。
- [sched-ext/scx](https://github.com/sched-ext/scx)。

未经外部规则确认，不把“旧作品可否参赛”“必须全新原创”“默认必须 LLM”等推测写成要求。可另向维护人确认内核与复用政策；本轮不发送邮件。
