# Security Policy lsm_capable 20260623-143856

运行环境：`192.168.1.121:/root/EulerPilot`

## 命令

```bash
cd /root/EulerPilot
bash tests/integration/test_security_policy.sh
```

## 结论

- 新增 `lsm_capable` scoped capability 规则通过。
- 测试使用 `CAP_SYS_ADMIN` 触发路径：scope 外 `unshare -m true` 正常，目标 cgroup 内被拒绝，Agent 退出后恢复。
- 既有 file_open、bprm、socket_connect、ptrace、PID/container/Pod target 回归全部通过。

## 关键 PASS

```text
PASS: security_policy lsm_capable blocks scoped CAP_SYS_ADMIN in target cgroup
PASS: security_policy pid target resolves to cgroup scoped enforcement
PASS: security_policy container_id target resolves to cgroup scoped enforcement
PASS: security_policy container runtime name target resolves to cgroup scoped enforcement
PASS: security_policy k8s pod name target resolves to cgroup scoped enforcement
```
