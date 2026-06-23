# Security Policy Integration 20260623-162418

主机：121 / EulerPilot-openEuler
路径：/root/EulerPilot/results/security_policy/integration-20260623-162418

本轮验证内容：

- 构建 Agent 与 security_policy_demo BPF 对象。
- 验证 audit 模式六类事件：lsm_file_open、lsm_bprm_check_security、sys_enter_execve、sys_enter_openat、sys_enter_connect、sys_enter_ptrace。
- 验证 anomaly_rules 第一版 burst_execve：连续 execve 触发用户态速率聚合，输出 operation=anomaly/result=observed。
- 回归验证 enforce 模式所有既有路径：file_open、bprm exec_prefix、socket_connect、path_prefix/file_access、ptrace_traceme、capable、PID/container/runtime/Pod scope。
- 验证 Agent 退出和 cleanup 后无 BPF link/pin 残留。

关键证据文件：

- test.log
- security_policy_events.anomaly-execve.jsonl
- security_policy_events.audit.jsonl
- security_policy_events.enforce.jsonl
- security_policy_events.readonly-dir.jsonl
- security_policy_events.capable.jsonl
- security_policy_events.pod.jsonl

结果：通过。
