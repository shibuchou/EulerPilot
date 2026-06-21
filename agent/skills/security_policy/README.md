# security_policy Skill

职责：把 EulerPilot 的安全策略能力从 demo 扩展为可审计、可强制、可回滚的运行时 Skill。当前仓库已注册正式 `security_policy`，并保留 `security_policy_demo` 作为兼容回归入口。

## 目标形态

正式 `security_policy` 应支持两种模式：

- `audit`：只记录 syscall/LSM 命中事件，不阻断行为，默认安全模式。
- `enforce`：只对显式 target 和规则命中的行为返回拒绝错误，必须能 rollback。

目标过滤应覆盖：

- 进程：PID/TGID/comm，用于最小调试。
- 路径：文件访问与程序执行路径，最低覆盖 `file_open` 和 `bprm_check_security`。
- 容器：通过 host PID、mount namespace、cgroup 或 container ID 解析目标。
- Kubernetes：后续由 Pod target 解析到 host 侧 namespace/cgroup；最小测试不依赖 Kubernetes。

事件输出应进入统一 AuditBus 或 JSONL，包含 hook、mode、action、target、pid/tgid、comm、path、rule_id、reason、errno 等字段。

## 当前完成度

当前 `security_policy` 的能力边界：

- BPF 程序：`bpf/security_policy_demo.bpf.c`
- Hook：`lsm/file_open`、`lsm/bprm_check_security`，以及 `tracepoint/syscalls/sys_enter_execve`、`tracepoint/syscalls/sys_enter_openat`、`tracepoint/syscalls/sys_enter_connect`、`tracepoint/syscalls/sys_enter_ptrace`
- 行为：`lsm/file_open` 可在 enforce 模式拒绝读取 `/root/EulerPilot/demo/security_policy_demo/secret.txt`；`lsm/bprm_check_security` 可在 enforce 模式拒绝执行 `/root/EulerPilot/demo/security_policy_demo/deny_exec.sh`；四类 syscall 当前只做 audit 观测，不参与阻断
- 用户态：`SecurityPolicyDemoSkill` 同时服务 `security_policy` 和 `security_policy_demo`；正式名读取 YAML v2 `targets + rules + target_ref`
- audit：attach BPF LSM + 四类 syscall tracepoint，但通过 `policy_map.enforce=0` 保持允许，命中后通过 ringbuf 写入 `reports/events/security_policy.jsonl`
- enforce：通过 libbpf 打开 `/root/EulerPilot/build/security_policy_demo.bpf.o` 并 attach LSM + tracepoint；当前只有 `lsm/file_open` 会返回拒绝
- 回滚：Agent stop/rollback 销毁所有 BPF link 和 object；正常路径不 pin link

当前尚未完成：

- 动态 path/process/container target map
- syscall 事件与动态规则、target map 的绑定

## 参考复用

- `third_party/reference/kata-lsm-ebpf`：重点参考 BPF LSM 多 hook、mount namespace profile mode、规则 map、audit ringbuf 和用户态 map 下发；可抽取成 openEuler 版 Security 模块，但不能整体复制。
- `third_party/reference/lmp-xdp-lsm`：参考最小 LSM hook 与 skeleton attach/detach，适合作为装载链路和 hook 形态对照。
- Android PSI 论文代码或 `third_party/reference/perfinsight-psi`：只复用 PSI 门控和实验方法；如要进入 EulerPilot，必须复制到项目目录后按 openEuler 24.03-LTS-SP3 的 BTF/libbpf/x86_64 环境重新编译。

## 最小验证

```bash
sudo tests/integration/test_security_policy.sh
```

脚本会构建 Agent 和 demo BPF 对象，先启动正式 `security_policy` 的 audit 模式，确认目标文件和 demo 可执行文件不被阻断，且写入 `lsm_file_open`、`lsm_bprm_check_security`、`sys_enter_execve`、`sys_enter_openat`、`sys_enter_connect`、`sys_enter_ptrace` 六类 BPF ringbuf hit 事件；再启动 enforce 模式，验证目标文件和 demo 可执行文件在策略生效期间被拒绝并写入 blocked hit 事件，并在 Agent 退出后恢复可访问。当前 BPF demo 硬编码 `/root/EulerPilot`，因此脚本会在其他路径下安全跳过并给出原因。

更完整的设计、验收口径和下一步清单见 `docs/security_policy_skill.md`。
