# Web Console 安全边界

Web Console 能触发部分 root 级演示脚本，因此默认只能作为本机 loopback 控制台使用。

## 默认访问方式

```bash
web_console/scripts/run_console.sh --daemon
ssh -L 18080:127.0.0.1:18080 openEuler-2403-LTS-SP4
```

默认监听：

```text
127.0.0.1:18080
```

## 强制规则

- 后端启动时检查工作目录必须是 EulerPilot 仓库根目录。
- `action_id` 必须精确匹配 `web_console/config/actions.yaml`。
- 不允许前端传任意 shell、任意路径或任意参数。
- `command` 使用 `spawn(..., { shell:false })`，不经过 shell 拼接。
- `command` 中禁止绝对路径和 `..` 路径段。
- `demo/lab/cleanup` 同一时间只允许一个运行。
- live/lab/cleanup 动作必须二次确认。
- 每个 action 有独立 `timeout_seconds` 和 `max_output_bytes`。
- 每个 job 内存日志只保留最近 1MB。
- 完整日志写入 `web_console/runtime/jobs/<job_id>.log`。
- cancel 会结束子进程组。

## demo_offline 口径

`demo_offline` 的 `kind` 是 `demo`，因此会占用 demo job slot，但它不创建 lab cgroup/veth/qdisc：

```yaml
safe_description: "离线演示，只读取现有证据和生成展示日志，不创建 lab cgroup/veth/qdisc。"
risk_description: "不会修改 EulerPilot 主干状态；运行期间会占用一个 demo job slot。"
```

## 非 loopback

不建议现场把控制台暴露到局域网。如果未来必须监听 `0.0.0.0`，必须设置：

```bash
export EULERPILOT_CONSOLE_TOKEN='long-random-token'
```

token 只能来自环境变量，不得写入仓库、配置、脚本或日志。
