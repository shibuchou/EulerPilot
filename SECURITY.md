# 安全策略

## 支持范围

EulerPilot 当前处于比赛交付阶段，安全问题处理范围包括：

- Agent、Skill、Policy Engine、Executor 的权限边界问题。
- Web Console 白名单动作、日志、鉴权和确认保护问题。
- 可能导致误操作非 EulerPilot lab 资源的脚本问题。
- 仓库中误提交 token、密码、私钥、kubeconfig 或其他敏感材料的问题。

## 报告方式

请优先通过私有渠道联系项目维护者，不要在公开 issue、报告、截图或日志中暴露敏感信息。

报告时建议包含：

- 受影响的文件、命令或接口。
- 复现步骤。
- 预期影响范围。
- 是否涉及真实主机、Kubernetes 集群或敏感凭据。

## 处理原则

- 默认不在 Web Console 中执行非白名单命令。
- 默认不操作生产网卡、系统 namespace、已有业务 Pod 或系统 cgroup。
- Kubernetes 验证必须使用独立 namespace、统一 label、有限资源配额，并在验证后清理。
- 不把明文密码、token、PAT、私钥或授权头写入仓库、脚本、文档或持久配置。
