知道。下面是我们项目来源中整理的完整 **《赛题参考与最终完成度标准》**。内容来源于比赛方现场宣讲投影，并结合 EulerPilot 的比赛目标、官方要求和项目实现路线整理。

# 一、比赛宣讲内容逐页整理

## 第 1 页：赛题宣讲

### 赛题宣讲：面向 openEuler 的自适应资源管控 Agent

**基于 sched_ext 的智能资源调度框架**

### 核心愿景

让操作系统具备 **Agent 化、自适应、可编程** 的能力，实现调度逻辑的灵活演进。

### 技术关键词

* eBPF
* sched_ext
* Agent

---

## 第 2 页：背景挑战

### 背景挑战：传统 Linux 调度器的瓶颈

#### Workload 复杂化趋势

* AI 推理，例如 vLLM，具有高吞吐需求；
* CI/CD 与微服务存在并发构建需求；
* 实时数据库对延迟高度敏感。

#### 传统调度器瓶颈

* 静态调度规则固定，演进缓慢；
* 缺乏细粒度的应用负载感知能力；
* 调整策略往往涉及内核重编译。

### 核心结论

> 所有 Workload 使用同一种策略已经不再适用。

---

## 第 3 页：sched_ext 技术突破

### 技术突破：sched_ext 让调度器可编程

#### 机制与策略分离

内核负责底层执行机制，调度策略决策转移到用户态或 eBPF 程序中实现。

#### 动态性与零重启

调度器策略可以随时动态加载或者替换，不需要重启系统内核，能够大幅提升策略迭代效率。

#### 引入 Agent 调度思想

通过用户态 Agent 实时感知系统 Workload 状态，并针对不同负载选择或者生成更适合的调度算法。

### 架构关系

```text
用户空间
  └─ Policy Agent / 策略代理
          ↓
      sched_ext 接口
          ↓
内核空间
  └─ Execution Layer / 执行层
```

### 核心价值

> 实现从“内核静态调度”到“智能 Agent 驱动”的范式演进。

---

## 第 4 页：赛题核心目标

### 赛题核心目标：构建操作系统 Agent 框架

#### 01. Agent Framework

* 提供 Skills 技能集；
* 提供 Policy 策略配置；
* 提供 Tool 机制。

#### 02. CPU Scheduling Agent

* 基于 scx 调度框架；
* 支持动态调度策略演进；
* 支持业务 Workload 感知。

#### 03. eBPF 扩展能力

* 动态网络策略执行；
* 实时安全监测增强；
* 细粒度资源限流。

---

## 第 5 页：赛题意义

### 赛题意义：走向 Agent-driven OS

```text
Static OS
传统静态调度
    ↓
Programmable OS
eBPF 可编程扩展
    ↓
Agent-driven OS
智能化自主驱动
```

### 工业界演进：内核级集群管理

* 类似 Kubernetes 调度器的内核版本；
* 借鉴 Google Borg 的规模化编排思想；
* 解决 Workload 复杂性带来的管理挑战。

### 前沿探索：AI Agent 深度共生

* 基于 sched_ext 框架实现策略解耦；
* AI Agent 实时感知识别复杂负载特征；
* 实现从手工调优到自动决策的范式转变。

---

## 第 6 页：核心任务

### 核心任务：自适应 CPU 调度 Agent

### 1. Workload 感知能力

需要能够识别不同类型的负载。

#### AI 推理

识别高度并行、计算密集型任务。

#### 编译任务

感知多阶段、混合型负载。

#### IO 密集型任务

监测频繁的中断、阻塞和上下文切换。

### 2. 动态策略切换

#### Latency-first

优先保障交互式、在线服务和延迟敏感任务的响应时间。

#### Throughput-first

优先提高批处理任务、后台任务和计算任务的整体吞吐量。

#### 自动切换

根据实时 Workload 感知结果自动调整调度策略。

策略选择公式：

[
Policy=\arg\max_{\pi}Q(W,\pi)
]

其中：

* (W)：当前 Workload 状态及特征；
* (\pi)：候选调度策略；
* (Q(W,\pi))：当前 Workload 使用对应策略时的收益评价。

### 3. 推荐实验场景

* AI 推理；
* 内核编译；
* 压缩任务；
* 网络压测。

---

## 第 7 页：eBPF 能力扩展

### Network Policy

* 流量分类与精准识别；
* 细粒度 QoS 限速策略；
* XDP 数据包高速拦截。

### Security Agent

* Syscall Tracing 追踪；
* 运行时异常行为监测；
* LSM 挂钩强制访问控制。

### Resource Control

* cgroup 动态配额调整；
* 内存回收压力深度感知；
* 容器资源利用率优化。

### 核心结论

> eBPF 实现了内核可观测性与 Agent 智能管控的深度解耦。

这里也明确了我们后续不能只完成一个简单的端口拒绝 Demo 或文件访问拒绝 Demo，而要向比赛方展示的完整能力靠拢：

```text
Network Policy
├─ 流量分类与精准识别
├─ QoS 细粒度限速
└─ XDP 高速拦截

Security Agent
├─ Syscall Tracing
├─ 运行时异常检测
└─ BPF LSM 强制访问控制

Resource Control
├─ cgroup 动态配额
├─ 内存与 PSI 压力感知
└─ 容器资源利用率优化
```

---

## 第 8 页：评分标准与演示建议

### 创新性：30%

* Agent 架构设计的新颖性；
* 调度策略的生成与优化机制。

### 性能提升：25%

* 与默认调度器进行量化性能对比；
* 展示混布场景中的优化比例。

### 功能完整性：25%

* 通过 sched_ext 实现 CPU Policy Agent；
* 支持扩展 eBPF 作为 Hook；
* 支持 Network Policy、Security Policy 等 Agent 扩展。

### 验证建议

* 提供 Benchmark 自动化脚本；
* 模拟 Workload 混合负载；
* 展示 Agent 化的自适应调节过程。

结合原有比赛规则，完整评分结构为：

| 评分项    |      分值 |
| ------ | ------: |
| 创新性    |      30 |
| 功能完整性  |      25 |
| 性能提升   |      25 |
| 代码质量   |      10 |
| 演示效果   |      10 |
| **总分** | **100** |

---

# 二、整理后的赛题定位

## 1. 赛题名称

**面向 openEuler 的自适应资源管控 Agent**

项目的准确定位是：

> 基于 eBPF 与 sched_ext，构建一个能够感知 Workload、自动选择调度策略并执行资源管控的操作系统 Agent 框架。

它不是单独实现一个 scx 调度器，也不是简单地为某些进程设置一次资源权重，而是要形成完整闭环：

```text
Workload 运行
    ↓
eBPF 低开销观测
    ↓
Agent 聚合系统状态
    ↓
识别 Workload 类型
    ↓
选择 Latency-first / Throughput-first 等策略
    ↓
sched_ext/scx 或其他 eBPF Hook 执行
    ↓
采集执行效果
    ↓
反馈并继续调整策略
```

EulerPilot 对应的系统架构是：

```text
Workloads
    ↓
eBPF Observer
    ↓
Agent Runtime
    ↓
Workload Analyzer
    ↓
Policy Engine
    ↓
Skill Manager
    ↓
CgroupExecutor / ScxExecutor
    ↓
Benchmark / Dashboard / Report
```

这与宣讲强调的“感知—决策—执行—反馈”路线一致。

---

## 2. 赛题要解决的核心问题

传统 Linux 调度器面向通用负载，往往不能充分区分：

* 延迟敏感服务；
* 高吞吐批处理任务；
* 高并行计算任务；
* 编译、压缩等阶段性负载；
* IO 和中断密集型负载；
* 后台干扰任务。

赛题希望系统不再对所有 Workload 使用同一套固定策略，而是由 Agent 根据系统实时状态进行：

```text
观测
→ 分类
→ 策略选择
→ 动态调节
→ 效果验证
```

---

# 三、三个核心交付对象

## 第一核心：Agent Framework

Agent Framework 必须体现：

* 标准化 Skill 接口；
* Tool 或 Policy 配置机制；
* 统一生命周期管理；
* 插件化和模块化扩展能力；
* 配置驱动启停；
* 观测、决策和执行模块解耦；
* 状态查询和诊断机制；
* 失败处理与安全回滚。

最终理想状态是所有功能都由同一个框架管理：

```text
EulerPilot Agent
├─ CPU Scheduling Skill
├─ Resource Control Skill
├─ Network Policy Skill
├─ Security Policy Skill
├─ Observation Skill
├─ Benchmark Skill
└─ Rollback Skill
```

而不是 CPU、网络、安全和资源控制各自写一套互不关联的程序。

---

## 第二核心：CPU Scheduling Agent

CPU Scheduling Agent 是赛题主任务，不是可选扩展。

必须完成：

* 真实集成 sched_ext；
* 真实加载 scx 调度器；
* 感知不同 Workload；
* 至少支持两种调度倾向；
* 根据负载自动切换或动态调整策略；
* 验证相对默认调度器的性能收益。

因此：

> cgroup v2 可以作为兼容后端、辅助控制手段和失败回退路径，但不能替代 sched_ext/scx 成为最终赛题核心成果。

至少需要具备两种策略：

```text
Latency-first
Throughput-first
```

也可以进一步扩展为：

```text
BALANCED
LATENCY_FIRST
THROUGHPUT_FIRST
INTERFERENCE_CONTROL
EMERGENCY_PRESSURE
```

---

## 第三核心：eBPF 扩展 Agent

CPU 调度之外，Agent Framework 还应扩展：

* Network Policy Agent；
* Security Policy Agent；
* Resource Control Agent；
* cgroup Agent；
* 可观测性和异常检测能力。

这些扩展必须复用统一的：

* Agent Runtime；
* Skill 接口；
* Policy Engine；
* 配置机制；
* 状态查询；
* 生命周期管理；
* 回滚机制。

不能只是几个相互独立的演示程序。

---

# 四、推荐验证场景

## 1. 宣讲直接建议的实验场景

* AI 推理；
* 内核编译；
* 压缩；
* 网络压测。

## 2. 结合 EulerPilot 当前条件的主性能场景

### Redis 混部场景

```text
Redis
+
stress-ng CPU 干扰
```

目标：

* 识别 Redis 为延迟敏感服务；
* 识别 stress-ng 为后台干扰任务；
* 动态激活 Latency-first；
* 降低 Redis P99 和 P999 延迟；
* 记录后台吞吐损失。

### Nginx 混部场景

```text
Nginx
+
CPU / 网络干扰
```

目标：

* 提高请求延迟稳定性；
* 减少尾延迟；
* 保持合理吞吐。

### 编译混部场景

```text
Linux 内核编译
+
交互式或在线服务
```

目标：

* 识别编译负载的阶段变化；
* 避免后台编译严重影响前台服务；
* 对比 Throughput-first 和 Latency-first。

### 压缩混部场景

```text
压缩任务
+
延迟敏感服务
```

目标：

* 识别计算密集型批处理任务；
* 根据业务压力动态改变其调度优先级。

## 3. 补充场景

* vLLM 或轻量 AI 推理；
* stress-ng CPU/IO 混合压力；
* 网络连接和带宽竞争；
* Kubernetes Pod 混部；
* 容器资源动态调节；
* PSI 内存压力测试。

---

# 五、最终完成度验收标准

以下可以作为比赛提交前的正式验收清单。

# 1. 目标环境完成度

| 验收项          | 最终通过标准                                          |
| ------------ | ----------------------------------------------- |
| openEuler 兼容 | 在 openEuler 24.03 LTS SP3 上能够编译、安装、启动、测试和卸载     |
| 环境检查         | 自动检测内核、BTF、cgroup v2、PSI、sched_ext、权限和依赖        |
| 一键构建         | 从明确的环境执行固定命令即可完成构建                              |
| 一键启动         | 能通过统一入口启动 Agent 和需要的 Skills                     |
| 一键清理         | cgroup、BPF link、pinned map、scx scheduler 均能安全卸载 |
| 系统恢复         | 测试完成后恢复默认调度器和网络、安全、资源状态                         |
| 业务隔离         | 测试不能影响 Kubernetes 集群及其他业务                       |

openEuler 24.03 LTS SP3 是硬性目标环境，不能只在 Ubuntu、WSL 或定制内核上运行。

对于 sched_ext，可以使用独立的 OLK 6.6 或比赛后续发布环境验证，但 openEuler 主环境上的 Agent Framework、cgroup、Observer、Network、Security 和 Resource Control 仍必须正常工作。

---

# 2. Agent Framework 完成度

必须具备：

* `Skill` 标准接口；
* Skill 注册和发现；
* `SkillRegistry`；
* `SkillManager`；
* YAML 或同类配置驱动；
* Policy 和执行器解耦；
* 启动、运行、停止、卸载和回滚接口；
* `list-skills`；
* `doctor-skills`；
* 状态查询；
* 新增 Skill 不需要修改 Agent 主循环；
* Skill 加载失败不会造成系统失控；
* Skill 可以独立启用和关闭；
* Skill 状态能够被报告和展示。

高分标准：

> CPU、网络、安全和资源控制均由同一个 Agent Framework 管理。

---

# 3. Workload 感知完成度

不能只根据进程名硬编码分类。

## 最低要求

* 读取真实 eBPF 或内核运行指标；
* 能够识别至少三类 Workload；
* 分类结果能够真正影响策略；
* 输出分类原因和关键证据；
* 无关系统进程不会持续污染决策。

## 推荐分类

```text
LATENCY_SENSITIVE
THROUGHPUT_BATCH
IO_INTENSIVE
BACKGROUND_NOISY
UNKNOWN
```

## 推荐观测证据

* Wakeup latency；
* Runtime；
* Context switch；
* Migration；
* CPU utilization；
* CPU PSI；
* Memory PSI；
* IO PSI；
* Runnable queue；
* IO wait；
* 中断行为；
* 网络行为。

## 高分标准

> 同一进程的运行特征发生变化后，Agent 能够改变分类或者策略，而不是始终根据 `comm` 得到固定结果。

进程名可以作为辅助证据，但不能成为唯一依据。

---

# 4. sched_ext/scx 完成度

这是判断是否真正完成赛题的关键部分。

必须达到：

1. scx 调度器可以真实加载；
2. 任务能够进入 sched_ext 调度路径；
3. Agent 分类结果能够传递至 BPF map；
4. 不同类型任务进入不同 DSQ、slice 或调度路径；
5. 至少存在 Latency-first 和 Throughput-first 两种策略；
6. 策略能够动态切换；
7. 切换不需要重启内核；
8. 调度器能够正常退出；
9. 异常时能够安全回退到默认调度器；
10. 提供调度统计和状态证据；
11. 能证明不同类别的任务确实执行了不同调度逻辑；
12. Agent 能够驱动 scx，而不是完全依靠人工操作。

不能只做到：

* 有一个 `.bpf.c` 文件；
* 程序能够编译但不能加载；
* 只有 `class_map`，但没有调度行为差异；
* 手动启动两个不同程序，冒充动态策略切换；
* 只通过 cgroup 权重实现全部优化；
* 只展示 `/sys/kernel/sched_ext` 存在；
* 只展示调度器启动日志，没有任务进入 DSQ 的证据。

---

# 5. 自适应闭环完成度

最终必须形成真实闭环：

```text
观测
→ 分类
→ 策略选择
→ 执行
→ 效果评估
→ 状态更新
```

至少需要：

* 两种以上策略；
* 自动触发条件；
* 明确状态机；
* 防抖机制；
* 冷却机制；
* 切换日志；
* 当前策略可查询；
* 切换前后指标可比较；
* 误判或者压力消失后能够自动恢复。

推荐状态机：

```text
NORMAL
  ↓
ARMED
  ↓
ACTIVE
  ↓
COOLDOWN
  ↓
NORMAL
```

状态含义：

### NORMAL

系统压力较低，保持默认或者平衡策略。

### ARMED

检测到潜在压力，但暂不立刻切换，等待持续证据。

### ACTIVE

压力满足条件，激活资源控制或调度优化策略。

### COOLDOWN

压力下降后不立即退出，进入冷却期，避免频繁振荡。

需要证明的不是“系统能够切换”，而是：

> Agent 因为观测到了具体压力，作出了可解释的策略决策，并且执行后性能发生了可验证的变化。

---

# 6. Network Policy Agent 完成度

比赛方明确提到：

* 流量分类与精准识别；
* 细粒度 QoS 限速；
* XDP 数据包高速拦截。

因此最终 Network Policy 不应该停留在单端口拒绝 Demo。

## 最低完成标准

* 指定 cgroup、容器或进程范围；
* 按 IP、端口或者协议识别；
* 支持 Allow/Deny；
* 策略命中计数；
* 动态加载；
* 动态卸载；
* 回滚后网络恢复；
* 不影响无关 Pod 或宿主机业务。

## 完整目标

### 流量分类与精准识别

至少支持以下维度中的多个：

* cgroup ID；
* Pod；
* 容器；
* 进程；
* 源 IP；
* 目标 IP；
* 源端口；
* 目标端口；
* TCP/UDP；
* 流量方向。

### 细粒度 QoS 限速

至少完成一种真实限速方式：

* TC ingress/egress；
* cgroup skb；
* Token Bucket；
* 每 cgroup 限速；
* 每端口限速；
* 每目标地址限速。

必须展示：

```text
无限速
→ Agent 下发 QoS 策略
→ 带宽或请求速率下降到目标范围
→ 命中计数增长
→ Agent 撤销策略
→ 网络性能恢复
```

### XDP 高速拦截

至少完成：

* XDP 程序真实挂载；
* 按规则执行 `XDP_DROP` 或 `XDP_PASS`；
* 命中统计；
* 可动态更新规则；
* 可正常卸载；
* 卸载后网络恢复。

高分标准：

> Network Skill 同时具备分类、限速和高速拦截三层能力，并由同一 Policy Engine 决策。

---

# 7. Security Agent 完成度

比赛方明确提到：

* Syscall Tracing；
* 运行时异常行为监测；
* LSM 挂钩强制访问控制。

最终不能只做一个固定文件拒绝 Demo。

## Syscall Tracing

至少能够：

* 跟踪指定 syscall；
* 按 PID、cgroup 或容器过滤；
* 记录调用次数、失败情况和参数摘要；
* 将事件发送到用户态 Agent；
* 避免采集全部系统调用导致过高开销。

推荐跟踪：

* `execve`；
* `openat`；
* `connect`；
* `ptrace`；
* `mount`；
* `setns`；
* `unshare`；
* `bpf`；
* `chmod`；
* `unlink`。

## 运行时异常监测

至少实现规则型异常识别，例如：

* 短时间大量 `execve`；
* 异常敏感文件访问；
* 异常外联；
* 容器内调用 `mount`、`ptrace`、`setns`；
* syscall 频率突增；
* 非预期子进程启动；
* 访问 `/proc/sys`、`/sys/kernel` 等敏感路径。

Agent 应输出：

* 异常对象；
* 异常类型；
* 触发时间；
* 触发证据；
* 风险等级；
* 采取的动作。

## BPF LSM 强制访问控制

至少完成：

* 精确到进程、cgroup 或路径；
* 记录安全事件；
* 对指定行为拒绝或者审计；
* 动态加载和卸载；
* 不影响无关业务；
* 失败时采用明确的安全策略；
* 回滚后访问行为恢复。

完整闭环：

```text
Syscall / LSM 事件
→ Security Skill 采集
→ 异常规则判断
→ Policy Engine 决策
→ Audit 或 Deny
→ 事件统计与展示
→ 策略撤销和恢复
```

---

# 8. Resource Control Agent 完成度

比赛方明确提出：

* cgroup 动态配额调整；
* 内存回收压力深度感知；
* 容器资源利用率优化。

## cgroup 动态配额

至少支持以下项目中的两项：

* `cpu.weight`；
* `cpu.max`；
* `cpuset.cpus`；
* `memory.high`；
* `memory.max`；
* `memory.low`；
* `io.max`；
* `io.weight`。

不能只是启动时写一次配置，而应当根据运行状态动态调整。

例如：

```text
CPU PSI 升高
→ 降低后台任务 cpu.weight
→ 必要时设置 cpu.max
→ 压力恢复后逐步恢复配额
```

## 内存压力深度感知

至少监测：

* `/proc/pressure/memory`；
* `memory.current`；
* `memory.events`；
* `memory.stat`；
* `memory.high` 触发情况；
* page reclaim；
* major fault；
* OOM 或 OOM kill 风险。

理想闭环：

```text
内存 PSI / reclaim 上升
→ Agent 判断持续压力
→ 调整后台容器 memory.high
→ 降低后台资源占用
→ 关键容器获得更多可用资源
→ 压力下降后恢复
```

## 容器资源利用率优化

至少完成：

* 识别容器或 Pod 对应 cgroup；
* 读取容器资源利用率；
* 区分关键服务和后台服务；
* 动态调整配额；
* 保证不会误操作系统 cgroup；
* 提供变更前后指标。

高分标准：

> Resource Control 与 CPU Scheduling Agent 共用同一策略中心，并能进行联合决策。

例如：

```text
调度层：把关键服务分配到 latency DSQ
资源层：提高关键服务 cpu.weight
内存层：限制后台容器 memory.high
网络层：限制后台容器带宽
```

---

# 9. eBPF 扩展能力统一验收标准

所有扩展 Skill 都必须展示：

```text
Agent 下发策略
→ eBPF Hook 或 cgroup 控制生效
→ 请求、流量或行为被处理
→ 统计数据增加
→ Agent 撤销策略
→ 系统恢复
```

以下情况不能算完整完成：

* 只有接口和目录；
* 只有 BPF 程序，没有用户态控制；
* 只有加载，没有命中证据；
* 只有命中，没有卸载和恢复；
* 依赖手工修改大量配置；
* 影响整个宿主机；
* 各 Demo 无法由统一 Agent 启动和管理。

---

# 10. 性能实验完成度

必须设计三类对照：

```text
A. 默认 Linux 调度器
B. 静态 scx 或固定策略
C. EulerPilot Agent 自适应策略
```

## 混布实验至少包括

* 一个前台关键 Workload；
* 一个后台干扰 Workload；
* 安静环境基线；
* 默认调度器混布；
* 固定策略混布；
* 自适应策略混布。

## 推荐保留的指标

### 延迟指标

* P50；
* P95；
* P99；
* P999；
* 最大延迟。

### 吞吐指标

* QPS；
* Requests/s；
* Operations/s；
* 编译耗时；
* 压缩耗时。

### 系统指标

* CPU 利用率；
* CPU PSI；
* Memory PSI；
* IO PSI；
* 上下文切换；
* 调度等待时间；
* 迁移次数；
* 运行队列长度。

### sched_ext 指标

* DSQ 入队次数；
* 不同 class 的任务数量；
* 调度次数；
* dispatch 次数；
* fallback 次数；
* rejected 次数；
* 调度器激活状态；
* class map 命中情况。

### Agent 指标

* 当前策略；
* 策略切换次数；
* ACTIVE 持续时间；
* 触发原因；
* 触发阈值；
* 冷却时间；
* 误判或者回退次数。

### 代价指标

* Agent CPU 占用；
* Agent 内存占用；
* eBPF 观测开销；
* 后台任务性能损失。

## 最终数据标准

* 每组至少 5 次正式运行；
* 关键实验建议 10～15 次；
* 保存每次原始数据；
* 输出均值；
* 输出中位数；
* 输出标准差；
* 输出置信区间；
* 所有图表能够从原始数据重新生成；
* 记录机器信息；
* 记录内核版本；
* 记录配置文件；
* 记录 Git commit；
* 记录完整执行命令；
* 说明性能收益；
* 说明优化代价；
* 说明适用边界；
* 不能只选择表现最好的一次。

---

# 11. 稳定性与安全完成度

需要完成：

* 连续运行稳定性测试；
* 多轮加载和卸载测试；
* Agent 异常退出回滚；
* scx 异常退出回退；
* Skill 初始化失败处理；
* BPF map 清理；
* BPF link 清理；
* pinned 对象清理；
* cgroup 清理；
* 无死锁；
* 无明显内存泄漏；
* 不破坏 Kubernetes 正常运行；
* 不修改生产 Workload 默认策略；
* 策略只作用于测试对象；
* 测试结束后系统恢复。

现场和正式测试建议全部使用：

* 独立 namespace；
* 专用 Pod；
* 专用 cgroup；
* 明确的 `nodeSelector`；
* 有限 CPU；
* 有限内存；
* 独立端口；
* 临时网络接口或明确网卡；
* 可以重复创建和清理的资源。

---

# 12. 代码质量完成度

需要具备：

* 清晰的模块目录；
* C/C++ 和 eBPF 代码风格统一；
* 关键接口注释；
* 完整错误处理；
* 清晰日志等级；
* 资源生命周期管理；
* RAII 或明确清理路径；
* 配置校验；
* 安全边界检查；
* 单元测试；
* Smoke 测试；
* 集成测试；
* 回归测试；
* 代码格式检查；
* 静态检查；
* 无无效占位代码；
* 无硬编码服务器密码；
* 无绝对依赖个人环境的路径；
* 无无法解释的魔法常量。

建议目录结构：

```text
EulerPilot/
├─ agent/
├─ bpf/
├─ sched/
├─ skills/
├─ configs/
├─ bench/
├─ experiments/
├─ scripts/
├─ demo/
├─ docs/
├─ reports/
└─ tests/
```

---

# 13. 文档完成度

应具备：

* README；
* 项目简介；
* 环境要求；
* 构建说明；
* 安装说明；
* 运行说明；
* 配置说明；
* 架构图；
* 模块图；
* 时序图；
* 状态机图；
* Policy 说明；
* Skill 接口说明；
* Benchmark 说明；
* 实验环境说明；
* 实验结果；
* 原始数据；
* 图表；
* 故障排查；
* 回滚说明；
* 安全注意事项；
* License；
* 第三方依赖说明；
* 开源声明；
* Release Tag。

技术报告需要包含：

* 作品链接；
* 赛题背景；
* 需求分析；
* 设计方案；
* 实现方案；
* 核心代码说明；
* 运行效果；
* 测试结果；
* 性能对比；
* 创新点；
* 局限性；
* 后续工作；
* 演示视频；
* 开源协作和开发过程数据。

---

# 14. 现场演示完成度

建议形成固定的 8～10 分钟演示流程：

```text
1. 展示 openEuler 环境和系统状态
2. 启动 EulerPilot Agent
3. 展示已加载 Skills
4. 展示 doctor 检查结果
5. 启动混合 Workload
6. 展示 Workload 分类
7. 展示分类证据
8. 展示 PsiGate 或策略状态变化
9. 展示 scx class_map、DSQ 和 stats
10. 展示业务 P99 或吞吐变化
11. 演示 Network Policy
12. 演示 Security Policy
13. 演示 Resource Control
14. 停止 Agent
15. 验证全部策略和资源完成回滚
```

演示必须满足：

* 一条主脚本启动；
* 每一步有明显输出；
* 不依赖临时手敲大量命令；
* 有超时机制；
* 有错误提示；
* 有环境预检；
* 有清理脚本；
* 有录屏备份；
* 网络中断时也能演示；
* 图表和现场输出可以互相印证；
* 即使某个增强 Skill 不可用，主线仍能安全继续；
* 演示完成后 Kubernetes 和系统状态正常。

---

# 六、EulerPilot 最终目标架构

最终应形成：

```text
                         EulerPilot Agent Runtime
                                  │
            ┌─────────────────────┼─────────────────────┐
            │                     │                     │
       Observation          Policy Engine         Skill Manager
            │                     │                     │
   ┌────────┼────────┐      ┌─────┼─────┐       ┌──────┼─────────┐
   │        │        │      │     │     │       │      │         │
sched    PSI      syscall  CPU  Network Security Resource Benchmark
events  pressure  events  Policy Policy  Policy  Policy   Skill
   │        │        │      │     │       │       │
   └────────┴────────┘      └─────┴───────┴───────┘
                                  │
                   ┌──────────────┼───────────────┐
                   │              │               │
             ScxExecutor    CgroupExecutor    eBPF Hooks
                   │              │               │
              DSQ / Slice    CPU/Memory/IO    XDP/TC/LSM
```

完整运行闭环：

```text
Workload / Pod / Process
        ↓
eBPF 采集运行特征
        ↓
Workload Analyzer 分类
        ↓
Policy Engine 结合 PSI、历史状态和业务目标决策
        ↓
Skill Manager 选择执行能力
        ↓
scx / cgroup / XDP / TC / LSM 执行
        ↓
Benchmark 与系统指标验证效果
        ↓
反馈至 Agent 状态机
        ↓
维持、增强、降级或撤销策略
```

---

# 七、最终完成状态的一句话定义

EulerPilot 达到比赛最终完成状态，应当是：

> 在 openEuler 目标环境上，使用真实 eBPF 数据感知 Workload，由统一 Agent Framework 自动分类，并在 Latency-first、Throughput-first 等策略之间进行动态切换，通过真实 sched_ext/scx 调度器执行调度，同时提供可运行的 Network Policy、Security Agent 和 Resource Control Skills，最终以可复现的混合负载 Benchmark 证明相对默认调度器的性能收益，并具备一键演示、故障回退、完整代码、原始数据和技术报告。

---

# 八、最重要的六条判断线

## 1. 不是静态框架，而是真实运行闭环

必须真实完成：

```text
观测
→ 决策
→ 执行
→ 验证
→ 反馈
```

## 2. 不是 cgroup-only，而是 sched_ext/scx 核心路径真实生效

cgroup 是重要后端和辅助能力，但最终 CPU Scheduling Agent 必须真实使用 sched_ext/scx。

## 3. 不是仅靠进程名硬编码，而是有运行时 Workload 感知

必须使用调度事件、PSI、运行时间、上下文切换、迁移等动态证据。

## 4. 不是人工调参，而是 Agent 自动决策和切换

必须存在自动触发、状态机、防抖、冷却和自动恢复。

## 5. 不是单次截图，而是可复现的量化性能证据

必须保留原始数据、多轮结果、统计分析、脚本和环境记录。

## 6. 不是零散 Demo，而是统一 Skills 框架下的 OS Agent 系统

CPU、网络、安全和资源控制应由统一框架管理，共享 Policy Engine、配置和生命周期。

---

# 九、按照比赛方宣讲，必须全部完成的扩展能力

我们此前已经明确：

> 为了得奖，比赛方汇报中提到的能力至少都要完成，再在此基础上扩展。

因此最终不能把下列内容视为“可选目录”或者“未来规划”。

## Network Policy 必须完成

* 流量分类与精准识别；
* 细粒度 QoS 限速策略；
* XDP 数据包高速拦截。

## Security Agent 必须完成

* Syscall Tracing 追踪；
* 运行时异常行为监测；
* LSM 挂钩强制访问控制。

## Resource Control 必须完成

* cgroup 动态配额调整；
* 内存回收压力深度感知；
* 容器资源利用率优化。

## CPU Scheduling Agent 必须完成

* Workload 动态感知；
* Latency-first；
* Throughput-first；
* sched_ext/scx 真实调度；
* 动态策略切换；
* 性能对比验证。

## Agent Framework 必须完成

* Skills 技能集；
* Policy 配置机制；
* Tool 机制；
* 统一生命周期；
* 配置驱动；
* 状态查询；
* 故障回滚；
* 扩展能力。

这就是我们目前用于指导 EulerPilot 最终开发、验收、技术报告和答辩准备的完整赛题参考。
这是