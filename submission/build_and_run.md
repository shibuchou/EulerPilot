# 编译与运行

更新时间：`2026-07-08`

## 1. 编译

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

## 3. 质量门禁

```bash
scripts/final_quality_gate.sh
```

预期：

```text
22/22 P0 pass
100-round smoke pass
5-round doctor pass
```

## 4. Web Console

```bash
cd /root/EulerPilot
web_console/scripts/run_console.sh --daemon
```

本地：

```bash
ssh -L 18080:127.0.0.1:18080 openEuler-2403-LTS-SP4
```

访问：

```text
http://127.0.0.1:18080
```

## 5. 推荐现场演示顺序

```text
环境检查
-> 查看 Skills
-> Agent 状态 JSON
-> Skill 诊断
-> 离线证据演示
-> 跨 Skill 联动实验
-> 清理现场资源
```
