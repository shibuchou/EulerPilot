# security_policy_demo

本目录是 SecurityPolicySkill 的最小 BPF 演示场景，不依赖 Kubernetes。当前 demo 验证两件事：Agent attach `bpf/security_policy_demo.bpf.c` 后，`lsm/file_open` 可以在 enforce 模式拒绝读取固定文件；audit 模式同时通过 ringbuf 输出 `lsm_file_open`、`sys_enter_execve`、`sys_enter_openat` 命中事件。

```text
/root/EulerPilot/demo/security_policy_demo/secret.txt
```

## 当前能力

- BPF hook：`lsm/file_open`、`tracepoint/syscalls/sys_enter_execve`、`tracepoint/syscalls/sys_enter_openat`
- 当前行为：`lsm/file_open` 命中固定路径后按 mode 决定 allow 或返回 `-EPERM`；`execve/openat` 只观测不阻断
- 当前模式：`audit` 通过 `policy_map.enforce=0` 允许访问并记录 observed hit；`enforce` 通过 `policy_map.enforce=1` 阻断固定 secret 文件并记录 blocked hit
- 当前输出：写入 `reports/events/security_policy.jsonl`，测试会归档为 `security_policy_events.audit.jsonl` 和 `security_policy_events.enforce.jsonl`
- 当前回滚：Agent `stop/rollback` 销毁所有 BPF link 和 object；正常退出不应留下 pin

## 快速验证

该 demo 目前硬编码 `/root/EulerPilot`，请在 openEuler 目标机或等价 Linux 环境的 `/root/EulerPilot` 下运行：

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
- audit 模式下，读取该文件仍成功，事件文件包含 `event_hook=lsm_file_open/sys_enter_execve/sys_enter_openat` 和 `result=observed`。
- enforce 模式下，读取该文件应失败，错误通常为 `Operation not permitted` 或 `Permission denied`，事件文件包含 `result=blocked`。
- Agent 退出或 cleanup 后，读取该文件恢复成功。

## 风险边界

这是正式 `security_policy` 的最小兼容 demo，不是完整 SecurityPolicySkill。不能把它直接用于真实业务路径，也不能扩大成全局文件拒绝策略。下一步需要补动态 target、路径/进程/容器过滤、`connect/ptrace` tracing、`bprm_check_security` enforce 和更完整的 ActionJournal 回滚证据。
