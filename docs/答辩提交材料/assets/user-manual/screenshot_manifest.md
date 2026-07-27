# EulerPilot 用户手册截图清单

更新时间：2026-07-26

本清单配套 `docs/用户手册.md`。当前只建立截图位和拍摄要求，不伪造截图；正式提交或录制视频前按清单补拍。

| ID | 目标文件 | 页面/命令 | 当前状态 | 拍摄/生成建议 | 说明 |
|---|---|---|---|---|---|
| UM-01 | `um-01-overview.png` | Web Console Overview | 待补拍 | 打开 `http://127.0.0.1:18081/?token=...`，显示 Overview、Host、Kernel、Git、Evidence | 展示 SP4 主验证 + SP3 强制兼容矩阵。 |
| UM-02 | `um-02-skills-agent.png` | Web Console Skills & Agent | 待补拍 | 点击 Skills & Agent，运行只读刷新 | 应能显示 9 个 Skill；safe 状态下 available=false 不等于功能缺失。 |
| UM-03 | `um-03-evidence.png` | Web Console Evidence | 待补拍 | 打开 Evidence & Live Demo 的 Evidence 区 | 按评分项展示 evidence。 |
| UM-04 | `um-04-policy-timeline.png` | Policy Engine Timeline | 待补拍 | 打开 Policy Engine Timeline | 展示 transaction_id 串联链路。 |
| UM-05 | `um-05-live-demo.png` | Live Demo | 待补拍 | 打开 Recommended Demo 和 Advanced/Optional 分区 | 截到 token/确认/cleanup 风险提示。 |
| UM-06 | `um-06-cli-dry-run.png` | `./build/eulerpilot-agent --config configs/agent.yaml --duration-s 5` | 待补拍 | 终端截图 | 展示 Agent 表格输出、mark legend、reason。 |
| UM-07 | `um-07-doctor-safe.png` | `./build/eulerpilot-agent --doctor-safe --config configs/agent.yaml` | 待补拍 | 终端截图 | 展示 safe doctor 不加载探针。 |
| UM-08 | `um-08-release-evidence.png` | `python3 scripts/collect_final_evidence.py --validate-release` | 待补拍 | 终端截图 | 展示 entries/missing/warnings。 |

## 建议截图命令

如果本地能通过隧道访问 Web Console，可以使用浏览器或 Playwright 截图；如果不方便自动化，手动截图也可以。截图文件统一放入本目录。

```bash
# SP4 上启动 Web Console 示例
cd /root/EulerPilot-candidate
export EULERPILOT_CONSOLE_TOKEN="$(openssl rand -hex 16)"
web_console/scripts/run_console.sh --daemon
```

```powershell
# 本地 PowerShell 隧道示例
ssh -N -L 18081:127.0.0.1:18080 openEuler-2403-LTS-SP4
```

## 自检要求

- 图片不得包含密码、token、私钥或无关个人信息。
- 图片中的结论必须来自当前候选仓库，不得混入旧仓库页面。
- 如果某张图未补拍，手册中只能保留“截图位”，不能写“截图已完成”。
