# 项目规则：openEuler 自适应资源管控 Agent

## 交流与工作目录

- 始终使用简体中文与用户交流，除非用户明确要求使用其他语言。
- 本项目主要工作位置为 openEuler 服务器 `EulerPilot-openEuler`，主机 `192.168.1.121`，用户 `root`，系统为 openEuler 24.03 LTS SP3。
- 服务器上的项目目录固定为 `/root/EulerPilot`；后续代码创建、编译、运行和测试默认都在该服务器目录中完成。
- 本地目录 `D:\code\Ubuntu\EulerPilot` 作为项目镜像和同步落点；只有在用户要求同步时，才从服务器同步到本地，再按用户要求同步到 GitHub。
- 在用户明确要求同步或推送之前，不要主动把服务器代码同步到本地或 GitHub，也不要主动 push。
- 项目主要开发语言为 C 和 C++；eBPF 内核侧程序优先使用 C，用户态 Agent/工具优先使用 C/C++，除非某个辅助脚本或工具链明显更适合用 Shell/Python。
- 需要 Linux 侧操作时，优先使用本机默认 WSL Ubuntu 22.04；对应 WSL 路径通常为 `/mnt/d/code/Ubuntu/EulerPilot`。
- 如需 sudo 密码，用户已在会话中提供；不要把明文密码写入配置文件、脚本、文档或日志。
- 交付物、中间实验数据、脚本和说明文档应集中放在本项目目录下，避免混入 `D:\code\Ubuntu\competition` 等其他已有项目目录。
- 如果本项目任务需要上网查询、搜索资料、打开网页或核验在线文档，应优先使用项目内 `skills/playwright` skill；读取该 skill 的 `SKILL.md` 后，使用 Playwright MCP 浏览器能力进行搜索、网页读取、快照和必要截图。
- 可按任务需要合理使用 MCP 能力：`context7` 用于查询库/框架官方文档和示例，`filesystem` 用于项目文件读写与目录管理，`githubmcp` 用于 GitHub 仓库、issue、PR 等协作操作，`playwright` 用于浏览器上网查询和网页交互，`sequential-thinking` 用于复杂方案设计、风险分析和多步骤推理。

## 赛题基本信息

- 比赛名称：第三届中国研究生操作系统开源创新大赛
- 赛题方向：系统创新
- 赛题名称：面向 openEuler 的自适应资源管控 Agent（社区赛题）
- 赛题联系人：段老师，`duanpengjie@huawei.com`
- 参考资料：<https://mp.weixin.qq.com/s/ewC8gTMRvm2NO6ywoproAA>

## 赛题内容

本项目需要设计并实现一个基于用户态调度的资源管控 Agent 框架。核心思路是由 Agent 感知 workload，再基于 `sched_ext` 调整 CPU 调度策略，生成或加载 `scx` 调度器，以优化 workload 性能。

在 CPU 调度 Agent 之外，项目还可以扩展 eBPF 作为 hook，由 Agent 作为策略决策中心，实现其他类型的 OS Agent，例如：

- network policy agent
- security policy agent
- resource control agent / cgroup agent

## 赛题要求

项目实现需要覆盖以下能力：

- 设计并实现一个基于用户态调度的资源管控 Agent 框架。
- 实现标准化工具接口和 Skills 能力接口，支持 Agent 快速构建与扩展。
- Agent 需要能够感知 workloads，并基于 `sched_ext` 调整 CPU 调度策略。
- 集成 `scx` 调度器，实现对 workload 性能的优化。
- 支持扩展 eBPF 作为 hook，实现 network policy agent、security policy agent、resource control agent 等功能。
- 提供完整的性能对比测试数据和可复现的实验环境。

## 最终交付要求

- 最终代码需能在 openEuler 24.03-LTS-SP3 上正常编译、运行和测试。
- 鼓励在更多 Linux 发行版上编译、运行和测试，但 openEuler 24.03-LTS-SP3 是硬性目标环境。
- 技术报告需要包含作品链接、设计方案、实现方案、运行效果/测试结果、演示视频、特色创新等内容。
- 系统创新赛道会重点考察项目代码、开发过程数据、技术报告和现场答辩表现。

## 评分细则

- 创新性（30 分）：Agent 架构设计的新颖性，调度策略的创新程度。
- 功能完整性（25 分）：workload 感知能力、调度策略调整的准确性和灵活性。
- 性能提升（25 分）：相比默认调度器的性能提升幅度。
- 代码质量（10 分）：代码结构、注释和可维护性。
- 演示效果（10 分）：现场演示的流畅度和说服力。

## 开发原则

- 优先围绕“可运行、可测试、可演示”的主线推进，不只做静态框架。
- 调度 Agent 主路径应优先覆盖：workload 观测、策略决策、`sched_ext/scx` 调度器切换或参数调整、性能对比验证。
- eBPF 扩展能力可以先按插件化或模块化接口预留，再逐步实现 network/security/resource control 等子 Agent。
- 性能实验需要保留基线数据、Agent 策略数据、测试命令、环境信息和可复现实验脚本。
- 代码结构应服务于比赛交付：核心 Agent、调度器集成、eBPF hooks、实验脚本、文档和演示材料应边界清晰。
- 涉及系统级能力时，优先在 WSL/openEuler VM/目标 Linux 环境中做真实编译运行验证；不能只以 Windows 侧静态检查作为完成依据。

## 参考仓库与复用边界

- 远端主交付仓库中的参考代码统一放在 `/root/EulerPilot/third_party/reference/`。
- 本地镜像中的参考代码统一放在 `D:\code\Ubuntu\EulerPilot\third_party\reference\`。
- 当前已确认可直接作为后续 network/security 扩展参考的目录包括：
  - `third_party/reference/lmp-xdp-lsm`
  - `third_party/reference/kata-lsm-ebpf`
  - `third_party/reference/perfinsight-psi`
- `lmp-xdp-lsm` 主要提供 `XDP` 与 `LSM` 方向的最小参考样例，后续 `NetworkPolicySkill` 优先参考其中的 `xdp/xacl_ip` 与公共装载代码。
- `kata-lsm-ebpf` 主要提供 `BPF LSM` 程序、用户态装载方式和最小头文件集合，后续 `SecurityPolicySkill` 优先参考其中的 `varmor_lsm` 与 `kata_lsm_agent` 相关代码。
- 这些目录都是“reference snapshot”，只用于方案设计、接口抽取和 openEuler 适配参考；不要把其中的历史备份、构建产物或与项目无关的大框架直接并入生产代码。
- 当前阶段先不同时开工 `Network Policy Agent` 与 `Security Policy Agent`；优先补强 Skills 注册机制、YAML 驱动能力和一个隔离的 eBPF 扩展示例，再逐步接入 network/security 子 Agent。
- `NetworkPolicySkill` 的第一版演示优先选择 `cgroup/connect4` 这类只作用于 demo cgroup 的 hook，不优先直接把 XDP 程序挂到网卡，避免影响远端 SSH 与主机网络。

## 建议目录结构

后续开发可按以下结构组织：

```text
EulerPilot/
  AGENTS.md
  README.md
  docs/
  agent/
  sched/
  ebpf/
  skills/
  experiments/
  scripts/
  reports/
  demo/
```
