# 贡献指南

EulerPilot 的当前目标是比赛交付前保持代码、证据和文档口径稳定。贡献时请优先保持小步、可验证、可回滚。

## 开发原则

- 不为展示效果修改 `agent/`、`bpf/`、`sched/` 主干语义。
- 新增功能优先放在独立 Skill、脚本或 Web Console 旁路目录中。
- 修改实验结论时必须同步更新 evidence manifest、报告和 README 入口口径。
- SP4 / sched_ext 表述必须准确：发行环境完成适配；sched_ext/scx 基于 SP4 官方源码自编译启用内核复核，不声称发行默认内核直接支持。
- Network、Security、Resource、Kubernetes 验证必须默认使用 EulerPilot lab 资源，并提供 cleanup。

## 本地检查

常用检查命令：

```bash
make agent
make unit-tests
./build/eulerpilot-agent --validate-config configs/agent.yaml
python3 scripts/collect_final_evidence.py --strict
bash scripts/final_quality_gate.sh
```

Web Console 检查：

```bash
cd web_console
npm ci
npm run lint
npm run test
npm run build
```

格式化入口：

```bash
make format
make format-check
```

## 提交要求

- Commit message 必须和实际 diff 匹配。
- 不提交 `.bak`、`.tmp`、`.old`、`core.*`、本地日志、私钥、token 或临时生成物。
- 结果目录较大时，确认其属于最终 evidence 或明确的实验阶段产物。
- 推送前检查：

```bash
git status --short
git log --oneline --decorate --max-count=8
```
