# 编译与运行

更新时间：`2026-07-20`

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

## 3. Evidence strict

```bash
python3 scripts/collect_final_evidence.py --strict
```

预期：

```text
entries=40
missing_required=0
warnings=0
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
22/22 P0 pass
100-round smoke pass
5-round doctor pass
```

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
