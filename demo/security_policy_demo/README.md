# security_policy_demo

本目录是 SecurityPolicySkill 的最小 BPF 演示场景。当前 demo 验证九件事：Agent attach `bpf/security_policy_demo.bpf.c` 后，用户态从 YAML target path、exec path、exec_prefix、dst_ip、dst_port、可选 cgroup_path、`type: pid`、`type: container_id`、`type: container` 和 `type: k8s_pod` 解析结果填充最多 8 项 BPF `target_map`，`lsm/file_open` 可以在 enforce 模式拒绝读取目标文件；`lsm/bprm_check_security` 可以在 enforce 模式拒绝执行 demo 脚本和目标 cgroup 内的可写目录前缀脚本；`lsm/socket_connect` 可以在 enforce 模式拒绝目标 cgroup 内的 IPv4 endpoint；带 cgroup_path 的 target 只在目标 cgroup 内阻断；PID target 可自动解析到 cgroup scope；container_id target 可在限定 cgroup tree 下解析到 cgroup scope；container runtime name 与 Kubernetes Pod 名称可解析到 cgroup scope；audit 模式同时通过 ringbuf 输出 `lsm_file_open`、`lsm_bprm_check_security`、`sys_enter_execve`、`sys_enter_openat`、`sys_enter_connect`、`sys_enter_ptrace` 命中事件。

```text
/root/EulerPilot/demo/security_policy_demo/secret.txt
/root/EulerPilot/demo/security_policy_demo/deny_exec.sh
```

## 当前能力

- BPF LSM hook：`lsm/file_open`、`lsm/bprm_check_security`、`lsm/socket_connect`
- BPF tracepoint hook：`tracepoint/syscalls/sys_enter_execve`、`tracepoint/syscalls/sys_enter_openat`、`tracepoint/syscalls/sys_enter_connect`、`tracepoint/syscalls/sys_enter_ptrace`
- 当前行为：`lsm/file_open` 命中 `target_map` 中任一文件路径后按 mode 决定 allow 或返回 `-EPERM`；`lsm/bprm_check_security` 命中 `target_map` 中任一精确执行路径或字面执行路径前缀后按 mode 决定 allow 或返回 `-EPERM`；`lsm/socket_connect` 命中 `target_map` 中任一 `dst_ip/dst_port` 后按 mode 决定 allow 或返回 `-EPERM`；如果 target 带 `cgroup_id`，BPF 会同时要求当前进程 cgroup id 命中；`type: pid`、`type: container_id`、`type: container` 和 `type: k8s_pod` 由用户态解析为 cgroup id 后复用同一 BPF 路径；四类 syscall 只观测不阻断
- 当前模式：`audit` 通过 `policy_map.enforce=0` 允许访问并记录 observed hit；`enforce` 通过 `policy_map.enforce=1` 阻断 demo secret 文件和 demo 执行脚本并记录 blocked hit
- 当前输出：写入 `reports/events/security_policy.jsonl`，测试会归档为 `security_policy_events.audit.jsonl`、`security_policy_events.enforce.jsonl`、`security_policy_events.dynamic.jsonl`、`security_policy_events.scoped.jsonl`、`security_policy_events.socket.jsonl`、`security_policy_events.exec-prefix.jsonl`、`security_policy_events.pid.jsonl`、`security_policy_events.container.jsonl`、`security_policy_events.runtime-container.jsonl` 和 `security_policy_events.pod.jsonl`
- 当前回滚：Agent `stop/rollback` 销毁所有 BPF link 和 object；正常退出不应留下 pin

## 快速验证

当前集成脚本仍以 `/root/EulerPilot` 作为 demo 目标路径基准，请在 openEuler 目标机或等价 Linux 环境的 `/root/EulerPilot` 下运行：

```bash
make agent security-policy-demo
sudo tests/integration/test_security_policy.sh
```

脚本会创建临时配置，只启用运行 demo 所需的 Skill，不修改仓库里的 `configs/skills.yaml`。失败路径会 kill Agent、调用 cleanup，并检查 secret 文件是否恢复可读。

手工清理入口：

```bash
sudo scripts/cleanup_security_policy_demo.sh
```

## 预期现象

- 策略未启动前，`cat demo/security_policy_demo/secret.txt` 成功。
- audit 模式下，读取该文件和执行 `deny_exec.sh` 仍成功，事件文件包含 `event_hook=lsm_file_open/lsm_bprm_check_security/sys_enter_execve/sys_enter_openat/sys_enter_connect/sys_enter_ptrace` 和 `result=observed`。
- enforce 模式下，读取该文件、执行 `deny_exec.sh`、目标 cgroup 内执行 `exec_prefix` 匹配的可写目录脚本和命中 scoped socket endpoint 应失败，错误通常为 `Operation not permitted` 或 `Permission denied`，事件文件包含 `result=blocked`。
- Agent 退出或 cleanup 后，读取该文件和执行 `deny_exec.sh` 恢复成功。

## 风险边界

这是正式 `security_policy` 的最小兼容 demo，不是完整 SecurityPolicySkill。当前 `target_map` 已能由 YAML 填充最多 8 组文件/执行路径/执行前缀、socket endpoint 和可选 cgroup 作用域，并在集成测试中验证两组 `/tmp/eulerpilot-security-policy.*` 动态目标、一组显式 cgroup scoped target、一组 scoped IPv4 socket target、一组 scoped writable-dir exec_prefix target、一组 PID 自动解析 target、一组 container_id cgroup tree 解析 target、一组 runtime container name 解析 target，以及一组 k8s pod name 解析 target；但它仍不是生产级路径/进程/Pod/container 过滤矩阵，也不能扩大成全局文件或网络拒绝策略。下一步需要补更多 LSM hook、异常规则和更完整的 ActionJournal 回滚证据。121 最新通过结果为 `results/security_policy/integration-20260622-145403`，122 最新通过结果为 `results/security_policy/integration-20260622-145716`。
