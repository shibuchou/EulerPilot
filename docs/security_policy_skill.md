# SecurityPolicySkill 正式化说明

本文面向比赛评委和后续 Agent，说明 EulerPilot Security Agent 的能力定位、当前完成度、参考代码复用边界和最小验收入口。当前仓库已经具备 `security_policy_demo` 最小闭环：构建 BPF LSM 程序，由 Agent attach 到 `lsm/file_open`，对固定 demo secret 文件返回拒绝，并在 Agent 退出时通过 BPF link fd 自动 detach。它还不是完整的正式 `security_policy` Skill。

## 能力定位

SecurityPolicySkill 的正式目标是把安全策略作为 EulerPilot 的运行时 Skill 接入统一控制面，支持 `audit` 和 `enforce` 两种模式。

- `audit` 模式：只观测并输出事件，不改变内核安全决策。该模式用于评估策略命中面、生成候选规则、验证 target 解析是否准确，应作为默认安全模式。
- `enforce` 模式：在明确 target 与规则后，由 BPF LSM hook 返回 `-EPERM` 或 `-EACCES` 等错误码阻断命中行为。该模式只能对显式选择的进程、容器、cgroup 或路径生效，不能作为全局默认。

BPF LSM 的边界需要写清楚：它运行在内核 LSM hook 链中，只能在已有 LSM 结果未拒绝时追加拒绝，不能覆盖前序 LSM 的拒绝结果；它要求目标内核启用 BPF LSM、挂载 bpffs，并以 root 或等价能力运行加载器。正式 Skill 必须先做能力探测，再决定是否进入 audit/enforce。

## Target 与过滤模型

正式实现应复用公共 `TargetResolver` 思路，把策略作用域从 demo 固定路径扩展为可声明 target：

- 进程过滤：PID/TGID、comm、父子进程关系，只用于短期调试或 demo。
- 路径过滤：文件路径、可执行路径、前缀/后缀/精确匹配，最低覆盖 `file_open` 与 `bprm_check_security`。
- 容器过滤：优先通过 host PID 解析 mount namespace、cgroup path、容器 ID，再把规则落到 mount namespace 或 cgroup 维度。
- Kubernetes 过滤：正式能力可以由 Pod/container target 解析到 host PID、cgroup、namespace；最小 integration 测试不依赖 Kubernetes。

当前 `bpf/security_policy_demo.bpf.c` 只实现了固定路径：

```text
/root/EulerPilot/demo/security_policy_demo/secret.txt
```

它没有动态 map、没有 ringbuf 事件，也没有真正按 `mode` 切换 audit/enforce；`mode` 目前只是配置和 snapshot 字段。文档和验收时应把它称为最小 BPF LSM enforce demo，不应称为正式 SecurityPolicySkill 完成。

## 事件输出

正式事件应进入统一 `AuditBus` 或等价 JSONL 文件，建议字段包括：

- `ts_ns`、`skill=security_policy`、`mode`、`action=audit|deny|allow`
- `hook=file_open|bprm_check_security|execve|openat|connect|ptrace`
- `target_ref`、`pid`、`tgid`、`comm`、`cgroup`、`mnt_ns`
- `path` 或 `exec_path`、`errno`、`rule_id`、`reason`

当前 demo 不输出 ringbuf 事件，最小验收以“attach 后目标文件被拒绝、Agent 退出后恢复访问、无 BPF link/pin 残留”为准。下一步成品化时，audit 模式必须先补事件输出，再开启 enforce。

## 回滚与清理

SecurityPolicySkill 涉及内核安全决策，默认回滚语义必须保守：

- BPF link 默认不 pin，Agent 退出或 rollback 时关闭 fd 即 detach。
- enforce 启动失败时立即销毁 link/object，并保留明确错误。
- 清理入口保留 `scripts/cleanup_security_policy_demo.sh`，用于删除 demo 相关 pin；当前 demo 正常路径不应留下 pin。
- integration 测试失败时必须 kill Agent、调用 cleanup、验证目标文件访问恢复。

## 参考代码复用边界

`third_party/reference/kata-lsm-ebpf` 是 SecurityPolicySkill 最接近正式化的参考快照，但不能整体并入生产路径。建议抽取为 openEuler 模块的部分：

- `varmor_lsm/varmor_lsm.bpf.c`：多 LSM hook 组织、mount namespace 级 profile mode、file/bprm/network/ptrace/mount 规则 map、ringbuf audit 事件。
- `varmor_lsm/varmor_lsm.c`：用户态加载、map 更新、profile/rule 下发、audit ringbuf 读取、link 生命周期管理。
- `varmor_lsm/apply_lsm_policy.c`：最小 CLI 的运行方式、信号退出、清理旧规则和 detach 流程。
- `kata_lsm_agent/kata_lsm_agent.bpf.c`：较小的 exec 路径 deny map、control map、ringbuf 事件结构，适合作为第一版 bprm enforce/audit 的轻量模板。

`third_party/reference/lmp-xdp-lsm` 与 `third_party/reference/lmp` 只作为 LSM hook 和 libbpf skeleton 装载方式参考：

- `lsm/lsm-connect/` 可参考最小 `lsm/socket_connect` 阻断样例。
- `lsm/lsm_bpf_monitoring/` 可参考 `bprm_check_security`、`file_open`、`cred_prepare` 等 hook 分布和 skeleton attach/detach。
- 不应复制其中旧项目的宽泛框架、硬编码 PID/文件名或不完整 verifier 处理。

用户 Android PSI 论文代码和 `third_party/reference/perfinsight-psi` 更适合复用“观测门控”和实验方法：用 PSI 作为高压力窗口筛选信号，减少低压状态下的安全事件噪声。Android 侧源码或二进制不能直接用于 openEuler；如要并入 EulerPilot，必须先复制到项目目录或 reference snapshot，删除 Android 设备/ARM64/ADB 绑定，再按 openEuler 24.03-LTS-SP3 的内核头、BTF、libbpf 和 x86_64 工具链重新编译验证。

## 最小验证入口

不依赖 Kubernetes 的验证入口：

```bash
sudo tests/integration/test_security_policy.sh
```

脚本默认只验证当前 demo 能力：

1. 检查必须在 `/root/EulerPilot` 运行，因为当前 Agent 和 BPF demo 仍硬编码该路径。
2. 检查 root、BPF LSM、bpffs、`bpftool`、`make`、`timeout` 等基础命令。
3. 执行 `make agent security-policy-demo`。
4. 使用临时 `agent.security.yaml` 和 `skills.security.yaml` 启用 `security_policy_demo`。
5. 启动 Agent，轮询读取 demo secret 文件，确认 attach 后被拒绝。
6. 等 Agent 正常退出，确认目标文件恢复可读，并调用 cleanup 验证无 demo 残留。

验收输出应包含 `PASS: target file is denied while policy is active` 和 `PASS: target file is readable after agent rollback`。

## 正式实现清单

- 注册正式 `security_policy` Skill，保留 `security_policy_demo` 作为回归用例。
- YAML schema v2 支持 `targets + rules + mode + target_ref`，默认 `mode: audit`。
- BPF 侧加入动态 map：target map、path rule map、exec rule map、control map、event ringbuf。
- 最低覆盖 `execve/openat/connect/ptrace` syscall tracing，以及 `file_open` 和 `bprm_check_security` 两类 BPF LSM enforce。
- 用户态接入 `TargetResolver`、`AuditBus`、`ActionJournal` 和 `CapabilityDetector`。
- integration 分层：本地 demo、container namespace、Pod target；Pod 测试只作为增强，不作为最小入口。
- 文档和脚本明确 openEuler 24.03-LTS-SP3 为硬性目标环境，其他发行版只作为兼容性参考。

## 验收口径

当前可验收：Security demo 的 BPF LSM 构建、attach、目标文件拒绝、Agent 退出恢复、cleanup 无残留。

下一阶段正式验收：audit 模式有结构化事件且不阻断；enforce 模式只阻断命中的目标进程/路径/容器；退出、异常和手工 cleanup 后无 BPF link、pin、map 规则残留；所有证据可由 README、integration 日志和结果目录追踪。
