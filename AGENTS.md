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
- 赛题宣讲整理参考：`docs/contest_briefing_reference.md`。该文档来源于比赛方现场宣讲投影整理，是后续技术路线、验收标准、报告口径和答辩材料的重要依据。

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
- 以得奖为目标时，不能把比赛宣讲中明确提到的 Network Policy、Security Agent、Resource Control 仅停留在简单 demo。最终应对齐 `docs/contest_briefing_reference.md` 的完成度标准：
  - Network Policy 至少覆盖流量分类、QoS 限速和 XDP 高速拦截三个方向；配置模型必须区分 cgroup、netdev、k8s_pod 等不同 target，XDP 只能默认挂到专用 lab veth/netns，不能挂生产管理网卡。
  - Security Agent 至少覆盖 syscall tracing、运行时异常监测和 BPF LSM 强制访问控制三个方向；syscall tracing 最低固定覆盖 `execve/openat/connect/ptrace` 四类，LSM 最低覆盖文件访问与程序执行两类强制控制。
  - Resource Control 必须从 CPU cgroup 扩展为 CPU + Memory + IO 自动闭环；资源动作必须有事务化流程、AuditBus 事件、ActionJournal 和 stop rollback 证据。
  - CPU Scheduling Agent 必须保持 sched_ext/scx 真实调度、Latency-first/Throughput-first/Mixed-Adaptive 策略和自动切换证据；Throughput-first 不能只做 smoke，必须进入正式 Benchmark。
- 下一阶段执行口径以 `docs/next_phase_plan_v2_1.md` 为准；`docs/next_phase_plan_v2.md` 只作为历史版本保留。
- 当前 Resource Control 已进入 CPU + Memory + IO + `target_ref` 自动闭环阶段，后续修改不得退化为只写 `cpu.weight`；`target_ref` 已可解析 cgroup/PID/container/container_id/k8s_pod 并落到目标 cgroup，`container_id`、runtime container name 和 `k8s_pod` 名称解析已具备 121/122 runtime target 集成测试证据；CPU quota 已具备 `cpu.stat usage_usec/nr_throttled/throttled_usec` 双机效果证据；后续增强重点是真实容器运行时/Kubernetes lab Pod 现场演示和更接近业务场景的多 workload quota benchmark。
- 当前 Security cred 类规则已完成 `task_fix_setuid`、`task_fix_setgid`、`task_fix_setgroups` 与 `cred_prepare`；`cred_transfer`、`cred_alloc_blank` 等更多 cred 生命周期 hook 已记录为后续项，暂不阻塞 Resource Control 阶段。
- 公共控制面必须前置：`TargetResolver` 统一解析 PID/cgroup/container/Pod/veth，`AuditBus` 统一事件格式，`ActionJournal` 统一回滚证据，`CapabilityDetector` 统一探测 BTF、BPF LSM、XDP、TC、cgroup v2、PSI、sched_ext 等能力。
- 所有正式 Skill 应统一支持 `observe/audit/enforce` 或兼容等价模式；`audit` 可作为历史 `dry-run` 的正式口径。
- 从 v2.1 开始，每完成一个阶段或阶段内关键子任务，必须同步更新对应文档，至少包括：`docs/progress_status.md`、该阶段设计/验证文档、相关脚本说明和结果目录说明。
- 每个项目自有顶层目录和核心模块目录必须有 `README.md`，说明目录作用、关键文件、运行或维护入口、当前完成状态和后续 TODO；新增目录时必须同时新增 README。
- 代码、实验脚本、结果和报告必须能通过 README 或 `docs/progress_status.md` 追踪到“当前完成到哪里、证据在哪里、下一步做什么”，避免只靠聊天记录或临时日志表达进度。
- 每次阶段收口前必须执行一次文档一致性检查：阶段计划、README、状态看板、质量门禁结果和实际目录内容不能互相矛盾。
- C/C++ 代码风格应接近 openEuler 常见开源项目：命名直接、结构清晰、错误路径显式、避免过度封装；系统调用、eBPF attach/detach、cgroup/TC/XDP/LSM 等有副作用代码必须有必要注释说明安全边界和回滚语义。
- 注释应服务于理解关键系统行为，不写空泛注释；对于 verifier 约束、hook 作用域、默认不 enforce、不会影响 SSH/管理网卡等安全前提，要在代码或相邻文档中写清楚。

## 参考仓库与复用边界

- 远端主交付仓库中的参考代码统一放在 `/root/EulerPilot/third_party/reference/`。
- 本地镜像中的参考代码统一放在 `D:\code\Ubuntu\EulerPilot\third_party\reference\`。
- 当前已确认可直接作为后续 network/security 扩展参考的目录包括：
  - `third_party/reference/lmp-xdp-lsm`
  - `third_party/reference/kata-lsm-ebpf`
  - `third_party/reference/perfinsight-psi`
- 用户明确允许在需要时参考其 GitHub/本地仓库中的 `katalsm`、`lightprobe`、`netscope` 和 `lmp`。使用这些仓库时只抽取与当前任务直接相关的 hook、观测、装载、策略或测试设计；若要把代码并入 EulerPilot，必须先形成最小 reference snapshot，注明来源和复用边界，再针对 openEuler 24.03-LTS-SP3 重新编译适配。
- `lmp-xdp-lsm` 主要提供 `XDP` 与 `LSM` 方向的最小参考样例，后续 `NetworkPolicySkill` 优先参考其中的 `xdp/xacl_ip` 与公共装载代码。
- `kata-lsm-ebpf` 主要提供 `BPF LSM` 程序、用户态装载方式和最小头文件集合，后续 `SecurityPolicySkill` 优先参考其中的 `varmor_lsm` 与 `kata_lsm_agent` 相关代码。
- `netscope` 可作为后续 Network observability、协议解析、连接路径证据补强参考；`lightprobe` 可作为轻量动态观测和运行时探针组织参考；`katalsm` 可作为 Security Agent/Kata 场景下 LSM 事件链路参考。
- 这些目录都是“reference snapshot”，只用于方案设计、接口抽取和 openEuler 适配参考；不要把其中的历史备份、构建产物或与项目无关的大框架直接并入生产代码。
- 当前 `NetworkPolicySkill` 和 `SecurityPolicySkill` 已有 demo 级闭环，但后续必须按争奖目标继续成品化；不要把单端口拒绝或单文件拒绝当作最终完成。
- `NetworkPolicySkill` 的基础演示优先选择 `cgroup/connect4` 这类只作用于 demo cgroup 的 hook，避免影响远端 SSH 与主机网络；成品化阶段再补 QoS/TC 和 XDP 高速拦截能力。
- 最终提交前应逐步去掉 `network_policy_demo`、`security_policy_demo` 等 demo 口径，改为 `network_policy`、`security_policy` 正式 Skill；历史 demo 可作为子能力或回归测试保留。

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
