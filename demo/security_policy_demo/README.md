# security_policy_demo

本目录是 SecurityPolicySkill 的最小 BPF LSM 演示场景，不依赖 Kubernetes。当前 demo 只验证一件事：Agent attach `bpf/security_policy_demo.bpf.c` 后，`lsm/file_open` 会拒绝读取固定文件：

```text
/root/EulerPilot/demo/security_policy_demo/secret.txt
```

## 当前能力

- BPF hook：`lsm/file_open`
- 当前行为：命中固定路径后返回 `-EPERM`
- 当前模式：配置里有 `mode: enforce`，但 BPF/Skill 尚未实现 audit/enforce 动态切换
- 当前输出：不输出 ringbuf 安全事件；只通过目标文件是否被拒绝来验证
- 当前回滚：Agent `stop/rollback` 销毁 BPF link 和 object；正常退出不应留下 pin

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
- 策略启动后，读取该文件应失败，错误通常为 `Operation not permitted` 或 `Permission denied`。
- Agent 退出或 cleanup 后，读取该文件恢复成功。

## 风险边界

这是 demo，不是正式 `security_policy` Skill。正式化前不能把它用于真实业务路径，也不能扩大成全局文件拒绝策略。下一步需要补动态 target、audit 事件、路径/进程/容器过滤和 ActionJournal 回滚证据。
