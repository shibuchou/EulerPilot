# 编译与运行

更新时间：`2026-07-27`

## 1. 编译

建议在 openEuler 24.03 LTS SP4 主验证仓库执行：

```bash
cd /root/EulerPilot
make agent
make unit-tests
make network-policy network-qos-tc network-xdp security-policy
```

## 2. 基础检查

```bash
./build/eulerpilot-agent --validate-config configs/agent.yaml
./build/eulerpilot-agent --list-skills
./build/eulerpilot-agent --status --json
./build/eulerpilot-agent --doctor-skills --config configs/agent.yaml
```

## 3. Evidence compact / release validation

```bash
python3 scripts/collect_final_evidence.py
```

v6 当前状态：

```text
entries=42
missing_required=0
warnings=0
```

旧 SP4 RUNS=10 pre-fix 结果仍由 `evidence/evidence_status_overrides.json` 保留历史状态。最终证据已在 formal artifact RUNS=10 后通过 release validation：

```bash
python3 scripts/collect_final_evidence.py --validate-release
```

如果只想验证而不改写仓库内 compact 文件，可以把输出定向到 `/tmp`：

```bash
python3 scripts/collect_final_evidence.py --strict \
  --output-json /tmp/eulerpilot_final_evidence_check.json \
  --output-md /tmp/eulerpilot_final_evidence_check.md
```

## 4. 质量门禁

```bash
scripts/final_quality_gate.sh
```

预期：

```text
SP4 final gate: 29/29 P0 + 100 smoke + 5 doctor-safe pass
SP3 compatibility gate: 10/10 pass
```

双环境 final gate 已完成；如源码或 benchmark 语义再次变更，必须重新执行对应门禁。

## 5. Web Console

SP4 主验证仓库启动：

```bash
cd /root/EulerPilot
web_console/scripts/run_console.sh --daemon
```

本地建立 SSH 隧道：

```bash
ssh -L 18080:127.0.0.1:18080 openEuler-2403-LTS-SP4
```

访问：

```text
http://127.0.0.1:18080
```

Web Console 验证：

```bash
cd web_console
npm ci
npm run lint
npm run test
npm run build
```

## 6. 推荐现场演示顺序

```text
环境检查
-> 查看 Skills
-> Agent 状态 JSON
-> Skill 诊断
-> 离线证据演示
-> Policy Engine 跨 Skill 联动实验
-> 清理现场资源
-> 展示 final_quality_gate 与 strict evidence 结果
```
