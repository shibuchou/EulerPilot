# Security Policy integration 20260622-195342

环境：`192.168.1.121`，openEuler 24.03 LTS SP3。

本目录是 `tests/integration/test_security_policy.sh` 在 121 上的通过结果。该轮在已有 `file_open`、`bprm_check_security`、`socket_connect`、四类 syscall tracing、target_map、cgroup/PID/container/Pod scope 的基础上，新增验证 `file_access=write`。

关键结论：

- `make agent security-policy-demo` 构建通过。
- `security_policy` 和 `security_policy_demo` 均可枚举。
- audit 模式记录 `lsm_file_open/lsm_bprm_check_security/sys_enter_execve/sys_enter_openat/sys_enter_connect/sys_enter_ptrace` 命中事件，不阻断访问。
- enforce 模式阻断目标文件、目标执行文件、scoped socket connect、scoped exec_prefix 执行。
- `file_access=write` 用例中，目标 cgroup 内读打开成功、写打开失败，scope 外写入成功。
- blocked 事件携带 `rule_id=deny_write_open`、`target_ref=write_secret`、`file_access=write`、`file_flags` 和 `cgroup_id/cgroup_path`。
- Agent 退出后访问恢复，cleanup 无 BPF link/pin 残留。

关键文件：

- `test.log`：完整 PASS 列表。
- `skills.file-access.yaml`：本轮 `file_access=write` 配置。
- `security_policy_events.file-access.jsonl`：写打开阻断事件证据。
- `file-access-read.txt` / `file-access-read.err`：目标 cgroup 内读打开结果。
- `file-access-write.txt` / `file-access-write.err`：目标 cgroup 内写打开阻断结果。
- `file-access-post-cleanup.txt` / `file-access-post-cleanup.err`：rollback 后写入恢复结果。
