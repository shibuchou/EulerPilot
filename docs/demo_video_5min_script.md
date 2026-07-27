# EulerPilot 五分钟演示讲稿（前端 + Agent + Doctor）

更新时间：`2026-07-27`

用途：用于录制 5 分钟左右的短视频。本文里的“解说词”就是录制时可以直接念出来的话，不是给 Codex 或开发者看的解释。

## 录制前准备

### SP4 服务器

```bash
cd /root/EulerPilot
# 如果 Web Console 已在运行，可以跳过启动；如果没有运行，执行下面两行
export EULERPILOT_CONSOLE_TOKEN="$(cat web_console/runtime/console.token 2>/dev/null || openssl rand -hex 16)"
web_console/scripts/run_console.sh --daemon
```

### 本地 PowerShell

```powershell
ssh -N -L 18081:127.0.0.1:18080 openEuler-2403-LTS-SP4
```

另开一个 PowerShell 读取 token：

```powershell
ssh openEuler-2403-LTS-SP4 "cat /root/EulerPilot/web_console/runtime/console.token"
```

浏览器打开：

```text
http://127.0.0.1:18081/?token=这里填上一步输出的token
```

录制窗口建议：左侧浏览器，右侧或下方保留一个 SSH 终端。

---

## 0:00 - 0:35 开场与项目定位

### 画面

打开 Web Console 的 `总览` 页面。

### 解说词

> 大家好，我现在演示的是 EulerPilot。它是一个面向 openEuler 的自适应资源管控 Agent，核心流程是：先通过 eBPF、PSI 和系统事件做观测，然后由用户态 Agent 做 workload 分类、策略决策和 Skill 编排，再执行资源控制、网络策略、安全策略和回滚。

> 这个演示界面把 Agent 状态、证据清单、doctor 诊断和白名单演示动作集中放在一起，方便我在短时间内展示 EulerPilot 已经具备的主要能力。

---

## 0:35 - 1:15 总览页面

### 画面

在 Web Console `总览` 页面展示 Host、Kernel、Git HEAD、Evidence、平台路径分工。

### 解说词

> 这里是总览页面。可以看到当前主验证环境是 SP4，运行在 123 这台机器上。项目同时保留 SP3 作为比赛要求的强制兼容环境。这里需要特别说明，cgroup v2 是发行内核上的稳定执行路径；sched_ext 和 scx 是在 SP4 官方源码自编译启用相关内核选项后验证的增强路径，不是说 SP4 默认发行内核直接支持 sched_ext。

> 页面上的 evidence 数量和 Git 状态来自仓库中的 manifest、report 和实际命令输出。这里我主要用它确认当前环境、代码版本和证据完整度。

---

## 1:15 - 2:05 Evidence 页面

### 画面

切到 `证据与现场演示` 页面，展示 Evidence 分组；不用运行长实验。

### 解说词

> 这一页是证据入口。我们把项目证据按 Agent 框架、调度与 PSI、性能结果、网络策略、安全策略、资源控制、Policy Engine、回滚和质量门禁进行分组。

> 这样做的目的，是让评审或者复现人员不用在 results 目录里手动找文件，而是可以从固定的 evidence manifest 进入。旧实验数据不会被删除，但是如果它是旧二进制、旧 baseline 或者不能证明同一 Agent 连续闭环，就会被状态覆盖文件标记成 historical、provisional 或 invalid，不能作为最终正向结论。

### 可选终端命令

```bash
cd /root/EulerPilot
python3 scripts/collect_final_evidence.py --validate-release
```

### 命令后解说词

> 这条命令会检查最终证据白名单。当前我们关注的是 required evidence 是否缺失，以及是否存在 warning。它不会临时拼接结果，也不会递归扫描所有目录。

---

## 2:05 - 2:55 Skills 与 Agent 页面

### 画面

切到 `Skills 与 Agent` 页面，展示已注册 Skills 和状态 JSON。

### 解说词

> 这一页展示的是 Agent 的 Skill 注册情况。可以看到这里有资源控制、PSI Gate、Policy Engine、网络策略、网络 QoS、XDP、安全策略等能力。这里的“诊断就绪”表示 Skill 已经注册，配置读取正常；它不等于 Skill 正在运行。只有启动 live 或 lab 任务时，运行中才会显示为“是”。

> 这些能力来自后端 Agent 的统一 Skill 框架，前端按钮只是把常用操作集中展示出来，便于现场演示和复现。

### 终端命令

```bash
cd /root/EulerPilot
./build/eulerpilot-agent --list-skills
```

### 命令后解说词

> 这里是后端 Agent 直接输出的 Skill 列表。前端页面读取的也是这些已有命令和状态，不是自己伪造一套模块列表。

---

## 2:55 - 3:45 Agent 状态 JSON

### 画面

点击 `刷新 Agent`，或者在终端执行 status 命令。

### 终端命令

```bash
./build/eulerpilot-agent --status --json | jq '.skills[] | {name, running, state, reason: .evidence.reason}'
```

如果没有 `jq`，使用：

```bash
./build/eulerpilot-agent --status --json
```

### 解说词

> 现在我看一下 Agent 的结构化状态。这里每个 Skill 都会有 running、state 和 reason。当前 reason 是 ok，state 是 created，说明配置和注册状态是正常的。running 是 false，是因为我现在只做只读状态检查，没有启动会修改系统状态的 live probe。

> 这也是我们前端做成“诊断状态”的原因，避免把只读状态下的 available false 误解成模块不可用。

---

## 3:45 - 4:35 Doctor Safe

### 画面

在 `Skills 与 Agent` 页面点击 `运行 doctor`，或者终端执行 doctor-safe。

### 终端命令

```bash
./build/eulerpilot-agent --doctor-safe --config configs/agent.yaml
```

### 解说词

> 这里运行的是 safe doctor。它只检查内核能力、文件、权限、依赖和 capability，不会创建 cgroup，不会加载 BPF，也不会挂载 TC、XDP 或 LSM 探针。

> 所以这里看到 Skill 是 safe-not-probed 是正常的。它表达的是：当前 doctor 是安全只读模式，没有对系统做 live 探测。真正的网络、XDP、安全 LSM 或 Policy Engine 联动，会在专门的集成测试或演示脚本里运行。

---

## 4:35 - 5:10 现场演示按钮和安全边界

### 画面

回到 `证据与现场演示` 页面，展示推荐演示按钮，不必点击长实验。可以点 `离线证据演示`，它只读 evidence。

### 可选操作

点击：`离线证据演示`

### 解说词

> 这里的按钮都是白名单动作，不允许前端传任意 shell 命令。只读检查可以直接运行，demo、lab 和 cleanup 这类会改变状态的动作需要 token 和确认，而且同一时间只允许一个 mutating job。

> 现场短视频里我主要展示环境检查、证据、doctor 和 Agent 状态。如果正式答辩需要 live 链路，可以再运行跨 Skill 联动实验，它会创建 EulerPilot 自己的 lab cgroup 和 lab veth，并在结束后 cleanup，不会操作生产网卡或已有业务 Pod。

---

## 5:10 - 5:35 收尾

### 画面

回到 `总览` 或 `证据与现场演示` 页面。

### 解说词

> 总结一下，EulerPilot 已经形成了从观测、分类、策略决策、Skill 执行到审计和回滚的闭环。这个界面展示的是入口和证据，后端 Agent 负责读取配置、识别 workload、管理 Skill、输出审计事件，并在需要时执行 rollback。

> 这个短视频主要展示项目的可运行入口和控制面；完整性能数据、Policy Engine live 链路、Kubernetes target 和 release evidence 可以在后续长版演示或答辩环节展开。

---

## 备用命令清单

```bash
cd /root/EulerPilot
./build/eulerpilot-agent --validate-config configs/agent.yaml
./build/eulerpilot-agent --list-skills
./build/eulerpilot-agent --status --json
./build/eulerpilot-agent --doctor-safe --config configs/agent.yaml
python3 scripts/collect_final_evidence.py --validate-release
```

## 不建议在五分钟短视频中运行

```bash
sudo tests/integration/test_policy_engine_security_network_resource.sh
sudo demo/demo_all_final.sh --mode live
sudo bench/redis/run_redis_sched_ext_compare.sh
sudo bench/nginx/run_nginx_sched_ext_compare.sh
```

原因：这些命令更适合长版答辩或正式复现，会占用时间，也可能创建 lab cgroup、veth、qdisc 或启动 benchmark。
