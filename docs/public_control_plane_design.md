# EulerPilot 公共控制面设计

更新时间：`2026-06-18`

对应阶段：`docs/next_phase_plan_v2_1.md` 阶段 A

## 1. 目标

公共控制面负责把 Network、Security、Resource、CPU Scheduling 四类 Agent 从“各自硬编码”收口为统一机制。

第一阶段只做最小可用，不引入动态插件、RPC 框架或复杂数据库：

```text
TargetResolver
  -> 统一解析 PID / cgroup / container / Pod / veth

CapabilityDetector
  -> 统一探测内核和系统能力

AuditBus
  -> 统一事件 JSONL schema

ActionJournal
  -> 统一记录副作用动作和回滚依据
```

## 2. 推荐代码位置

```text
agent/include/target_resolver.hpp
agent/include/capability_detector.hpp
agent/include/audit_bus.hpp
agent/include/action_journal.hpp

agent/src/target_resolver.cpp
agent/src/capability_detector.cpp
agent/src/audit_bus.cpp
agent/src/action_journal.cpp
```

对应 README：

- `agent/README.md`
- `agent/skills/README.md`
- `configs/README.md`
- `docs/skills_yaml_plan.md`

## 3. TargetResolver

### 3.1 职责

把配置中的目标引用解析为内核可使用对象。

输入可以是：

- PID
- cgroup path
- cgroup id
- Kubernetes namespace/name
- netdev ifname

输出建议结构：

```cpp
struct TargetIdentity {
    std::string name;
    std::string type;          // pid | cgroup | k8s_pod | netdev
    int pid = -1;
    uint64_t cgroup_id = 0;
    std::string cgroup_path;
    std::string container_id;
    std::string pod_namespace;
    std::string pod_name;
    std::string netns_path;
    std::string ifname;
    int ifindex = 0;
};
```

### 3.2 最小实现

- `pid -> /proc/<pid>/cgroup -> cgroup_path`
- `cgroup_path -> stat/fhandle or helper -> cgroup_id`
- `netdev ifname -> if_nametoindex`
- `k8s_pod` 第一版只允许 `eulerpilot-lab`，缺少集群环境时返回 `unsupported`

### 3.3 安全边界

- `enforce` 模式默认拒绝作用于非 lab 目标。
- XDP target 必须是显式 netdev，不能从 cgroup 推断。
- TC target 必须解析到 netdev、veth 或 pod veth。

## 4. CapabilityDetector

### 4.1 探测项

| 能力 | 探测方式 |
|------|----------|
| BTF | `/sys/kernel/btf/vmlinux` |
| ring buffer | libbpf feature 或最小程序加载 |
| BPF LSM | `/sys/kernel/security/lsm` 包含 `bpf` |
| XDP | `ip link` + generic fallback |
| TC | `tc qdisc show` / `tc filter show` |
| cgroup v2 | `/sys/fs/cgroup/cgroup.controllers` |
| PSI | `/proc/pressure/{cpu,memory,io}` |
| sched_ext | `/sys/kernel/sched_ext` 或 scx 加载 smoke |
| memory.reclaim | 目标 cgroup 下 `memory.reclaim` |
| Kubernetes | `kubectl`、container runtime、cgroup driver |

### 4.2 输出

```cpp
struct CapabilitySnapshot {
    bool has_btf;
    bool has_ringbuf;
    bool has_bpf_lsm;
    bool has_xdp;
    bool has_tc;
    bool has_cgroup_v2;
    bool has_psi_cpu;
    bool has_psi_memory;
    bool has_psi_io;
    bool has_sched_ext;
    bool has_memory_reclaim;
    std::map<std::string, std::string> evidence;
};
```

## 5. AuditBus

### 5.1 事件 schema

```json
{
  "timestamp": "",
  "event_id": "",
  "skill": "",
  "policy_id": "",
  "rule_id": "",
  "mode": "audit",
  "target": {
    "pid": 0,
    "cgroup_id": 0,
    "container_id": "",
    "namespace": "",
    "pod": ""
  },
  "operation": "",
  "evidence": {},
  "action": "",
  "result": "",
  "severity": "info"
}
```

### 5.2 路径

默认目录：

```text
reports/events/
```

按 Skill 拆分：

```text
reports/events/network_policy.jsonl
reports/events/security_policy.jsonl
reports/events/resource_control.jsonl
reports/events/cpu_scheduling.jsonl
reports/events/policy_engine.jsonl
```

## 6. ActionJournal

### 6.1 职责

记录所有可回滚副作用，包括：

- cgroup 控制器旧值和新值
- BPF link
- pinned map
- qdisc/class/filter
- XDP interface
- policy id / rule id
- 是否已恢复

### 6.2 路径

```text
run/eulerpilot/action_journal.json
```

### 6.3 最小流程

```text
begin(action)
  -> record old state
  -> apply new state
  -> verify
  -> mark applied

rollback(action)
  -> read journal
  -> restore old state
  -> verify
  -> mark restored
```

## 7. 与 SkillManager 的关系

启动顺序建议：

```text
load config
  -> CapabilityDetector::probe()
  -> TargetResolver::load_targets()
  -> SkillManager::load_from_yaml()
  -> Skill::probe()
  -> Skill::init()
  -> Skill::start()
```

停止顺序建议：

```text
SkillManager::stop_all()
  -> reverse order
  -> Skill::rollback()
  -> ActionJournal::recover_pending()
```

## 8. 阶段 A 验收

阶段 A 结束时至少应满足：

- 设计文档存在并与 `docs/skills_yaml_plan.md` 不冲突。
- `--doctor-skills` 能输出公共能力探测摘要。已完成，见 `reports/final_quality_gate_20260618_control_plane.log`。
- `CapabilityDetector` 已接入 doctor；`TargetResolver / AuditBus / ActionJournal` 已提供可编译最小接口，后续正式 Skill 接入时继续扩展。
- `--status --json` 尚未实现，保留为下一阶段 CLI 增强项。
- `reports/final_quality_gate_20260618_stage_a.log` 和 `reports/final_quality_gate_20260618_control_plane.log` 作为当前基线门禁证据。

## 9. 下一步实现顺序

1. 增加 `CapabilityDetector`，先接入 `--doctor-skills` 输出。
2. 增加 `TargetResolver`，先支持 cgroup/netdev。
3. 增加 `AuditBus`，先支持 JSONL 追加写入。
4. 增加 `ActionJournal`，先支持 cgroup 旧值恢复和 BPF link 记录。
5. 把 Network/Security/Resource 后续实现全部接入这四个公共模块。
