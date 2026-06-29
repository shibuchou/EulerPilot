# openEuler 24.03 LTS SP4 验证计划

更新时间：`2026-06-29`

SP4 不作为 EulerPilot v3.1 完成条件。本文件只定义 SP4 发布后的验证入口和记录格式，主开发与当前证据仍以 openEuler 24.03 LTS SP3 的 121/122 双机结果为准。

## 目标

在 SP4 正式发布并准备好测试机后，验证 EulerPilot 在 SP4 上的基础能力兼容性：

- Agent 编译与基础启动。
- cgroup v2 CPU/Memory/IO controller。
- BTF、BPF LSM、XDP、TC、sched_ext/scx 能力探测。
- Security、Network、Resource Control、Policy Engine 的核心集成测试。

## 不阻塞事项

- SP4 下载、Release Notes 和镜像链接发布前，不把 SP4 作为 v3.1 主平台。
- Kubernetes/真实 runtime/真实 Pod veth 仍作为 v3.2 第一优先级，不放入 v3.1 完成条件。

## 检查入口

```bash
scripts/check_sp4_env.sh
```

脚本只做环境探测，不修改系统状态。若检测到 SP4，再继续执行：

```bash
make agent
./build/eulerpilot-agent --validate-config configs/agent.yaml
./build/eulerpilot-agent --list-skills
./build/eulerpilot-agent --doctor-skills --config configs/agent.yaml
sudo tests/integration/test_policy_engine_security_network_resource.sh
sudo scripts/final_quality_gate.sh
```

## 结果记录

SP4 验证结果建议保存到：

```text
results/sp4-validation/<timestamp>/
```

至少包含：

- `os-release.txt`
- `uname.txt`
- `check_sp4_env.log`
- `agent_validate_config.log`
- `doctor_skills.log`
- `policy_engine_security_network_resource.log`
- `final_quality_gate.log`
- `summary.md`

## 发布链接记录

SP4 正式发布后再补入：

- openEuler 下载页：待确认
- SP4 Release Notes：待确认
- SP4 ISO/镜像源：待确认
- 对应 kernel、bpftool、clang/llvm 版本：待确认