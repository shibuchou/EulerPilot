# EulerPilot Skills 与 YAML 能力改造规划

更新时间：`2026-06-12`

## 1. 当前判断

当前项目主线已经完成 `resource control agent` 的真实闭环，但 `Skills` 能力仍然偏“目录预留 + 文档描述”，缺少一个可验证的统一注册与启停机制。

因此，下一阶段不应同时开工 `Network Policy Agent` 和 `Security Policy Agent`，而应先补：

1. 轻量 `Skill` 注册机制
2. YAML 驱动的能力启用与参数传递
3. 一个隔离的 eBPF 扩展示例

这三件事完成后，network/security 扩展才会真正有工程说服力。

结合最新评估，当前阶段的范围应明确收口为：

```text
必须完成
-> Skill 基类
-> SkillRegistry 静态工厂
-> YAML 真正驱动启停
-> --list-skills
-> --doctor-skills
-> ResourceControlSkillAdapter
-> PsiGateSkillAdapter
-> 一个隔离的 NetworkPolicySkill Demo

可以延后
-> SecurityPolicySkill Demo
-> 动态库热加载
-> memory / io PsiGate
-> 完整 TC/XDP 防火墙
-> 大规模目录重构
```

## 2. 目标

目标不是实现复杂插件系统，而是证明：

```text
新增一个 Skill
-> 不侵入核心 runtime
-> 可以通过统一接口注册
-> 可以由 YAML 启用或禁用
-> 可以统一输出状态
-> 可以统一回滚
```

## 3. 规划原则

- 不做动态库热加载
- 不做通用 RPC 插件框架
- 不重写已经跑通的 `resource control` 主线
- 先用适配器封装既有能力
- 先做一个 `network_policy_demo`，`security_policy_demo` 延后
- 不为了补接口而回头扰动已经冻结的主实验链路

## 4. 建议新增接口

建议后续在远端主线代码中新增：

```text
agent/include/skill.hpp
agent/include/skill_registry.hpp
agent/include/skill_manager.hpp
agent/include/builtin_skills.hpp
agent/src/skill_registry.cpp
agent/src/skill_manager.cpp
agent/src/builtin_skills.cpp
```

接口建议保持最小可用：

```cpp
class Skill {
public:
    virtual ~Skill() = default;
    virtual std::string name() const = 0;
    virtual bool probe() = 0;
    virtual bool init() = 0;
    virtual bool start() = 0;
    virtual SkillSnapshot snapshot() = 0;
    virtual bool apply(const SkillAction& action) = 0;
    virtual bool rollback() = 0;
    virtual void stop() = 0;
};
```

状态结构建议：

```cpp
struct SkillSnapshot {
    std::string skill_name;
    bool available;
    bool running;
    std::string state;
    std::map<std::string, std::string> evidence;
};
```

动作结构建议：

```cpp
struct SkillAction {
    std::string action;
    std::map<std::string, std::string> parameters;
};
```

统一协调层建议增加：

```cpp
class SkillManager {
public:
    bool load_from_yaml(const SkillRegistry& registry, const AgentConfig& config);
    bool start_enabled_skills();
    bool rollback_all();
    void stop_all();
};
```

约束：

- `rollback()` 是每个有副作用 Skill 的生命周期方法
- `rollback` 不是与 `resource_control` 并列的独立业务 Skill
- `rollback_all()` 和 `stop_all()` 采用倒序执行

## 5. 注册机制规划

第一版只做静态工厂注册：

```cpp
using SkillFactory = std::function<std::unique_ptr<Skill>()>;

class SkillRegistry {
public:
    void register_factory(const std::string& name, SkillFactory factory);
    std::unique_ptr<Skill> create(const std::string& name) const;
    std::vector<std::string> list() const;
};
```

这足以证明：

```text
Skill 新增
-> factory 注册
-> runtime 通过名字创建
-> YAML 控制启用
```

推荐把内建能力注册集中到单点文件，而不是散落在 `runtime.cpp`：

```cpp
void register_builtin_skills(SkillRegistry& registry)
{
    registry.register_factory(
        "resource_control",
        [] { return std::make_unique<ResourceControlSkillAdapter>(); });

    registry.register_factory(
        "psi_gate",
        [] { return std::make_unique<PsiGateSkillAdapter>(); });

    registry.register_factory(
        "network_policy_demo",
        [] { return std::make_unique<NetworkPolicySkill>(); });
}
```

这样以后新增 Skill 时只需要修改：

```text
builtin_skills.cpp
+ 新增 Skill 文件
+ skills.yaml
```

而不是回头扰动核心 Runtime 流程。

## 6. 既有能力适配顺序与分层

### 6.1 第一批必须适配

- `resource_control`
  - 封装当前 `CgroupExecutor / ScxExecutor`
- `psi_gate`
  - 封装当前 `PsiGate` 状态与触发逻辑

### 6.2 第二批预留

- `network_policy_demo`

### 6.3 第三批可选

- `benchmark`
- `report`
- `security_policy_demo`

建议明确区分两类能力：

```text
RuntimeSkill
-> resource_control
-> psi_gate
-> network_policy_demo

ToolSkill
-> benchmark
-> report
```

第一版目标是先把 `RuntimeSkill` 跑通；`benchmark` 和 `report` 不进入 Agent 热路径，不强制接到同一启动语义里。

## 7. YAML 能力规划

当前建议把 `skills.yaml` 从说明性文件升级为真正驱动文件。

示例：

```yaml
skills:
  - name: resource_control
    enabled: true
    backend: cgroup_v2
    mode: mixed_profile

  - name: psi_gate
    enabled: true
    trigger_mode: psi
    activate_threshold: 2
    cooldown_rounds: 5

  - name: network_policy_demo
    enabled: false
    hook: cgroup_connect4
    cgroup_path: /sys/fs/cgroup/eulerpilot/demo-net
    mode: audit
    rules:
      - action: deny
        dst_port: 6379

  - name: security_policy_demo
    enabled: false
    hook: lsm
    profile: deny_mount_ptrace
```

如果后续补 `ToolSkill`，建议另设工具配置，例如：

```yaml
tools:
  benchmark:
    enabled: true
    workload: redis
  report:
    enabled: true
```

## 8. runtime 行为规划

Runtime 启动流程建议为：

```text
读取 agent.yaml
-> 读取 skills.yaml
-> register_builtin_skills(registry)
-> create(name)
-> probe()
-> init()
-> start()
-> 输出 snapshot
```

建议增加两个可验证命令：

```bash
./build/eulerpilot-agent --list-skills
./build/eulerpilot-agent --doctor-skills
```

输出目标：

```text
resource_control    available=yes running=yes
psi_gate            available=yes running=yes
network_policy_demo available=yes running=no
security_policy_demo available=no reason=not-built
```

## 9. network/security 的后续接入顺序

### 9.1 NetworkPolicySkill Demo

第一优先级。

建议优先采用：

```text
cgroup/connect4
```

而不是直接把第一版 Demo 挂到 XDP。

原因：

- 只影响 demo cgroup，对系统和 SSH 风险更低
- rollback 简单，detach cgroup hook 即可
- 演示对象更明确，便于绑定到指定测试进程
- 更适合做 Skills 生命周期演示

建议第一版路径为：

```text
创建 /sys/fs/cgroup/eulerpilot/demo-net
-> 将测试进程加入 demo cgroup
-> 挂载 cgroup/connect4 程序
-> 读取 YAML 端口策略
-> 更新 policy_map
-> 允许或拒绝 outbound connect
-> 输出 allow_count / deny_count
-> rollback 后恢复
```

参考来源建议分两层：

- 首选 `libbpf-bootstrap` 或最小 `cgroup/connect4` 样例做第一版实现
- `third_party/reference/lmp-xdp-lsm/xdp/` 作为第二阶段 XDP 扩展参考

建议演示目标：

- 从 YAML 读取一条简单规则
- 加载 `cgroup/connect4` 程序
- 写入 policy map
- 对单一目标端口做 allow / deny 演示
- 输出命中计数和回滚状态

### 9.2 SecurityPolicySkill Demo

第二优先级，放在 NetworkPolicySkill 之后。

建议参考：

- `third_party/reference/lmp-xdp-lsm/lsm/`
- `third_party/reference/kata-lsm-ebpf/varmor_lsm/`
- `third_party/reference/kata-lsm-ebpf/kata_lsm_agent/`

建议演示目标：

- 只做一个最小 `BPF LSM` 策略
- 用明确的 `deny_mount` 或 `deny_ptrace` 作为示例
- 不引入 Kubernetes、Android 或 Kata 的额外复杂度

## 10. 实施顺序

建议按下面顺序推进：

1. 补 `Skill` 接口与 `SkillRegistry`
2. 补 `SkillManager` 与 `builtin_skills`
3. 把 `resource_control / psi_gate` 适配进统一接口
4. 让 `skills.yaml` 真正驱动启用
5. 增加 `--list-skills / --doctor-skills`
6. 做 `network_policy_demo`
7. 最后再评估是否需要做 `security_policy_demo`

## 11. 当前一句话结论

当前最值得补的不是同时开工两个新 Agent，而是先把：

> Skills 接口、YAML 驱动能力和一个隔离的 eBPF 扩展示例做成可验证闭环。
