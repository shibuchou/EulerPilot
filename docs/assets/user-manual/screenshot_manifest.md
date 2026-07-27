# EulerPilot 用户手册截图清单

更新时间：2026-07-27

本清单配套 `docs/用户手册.md`。当前已补充 27 张 PNG 截图和一份 PDF 版截图清单，截图文件采用时间戳命名；正文已直接引用这些图片。下表保留每类截图的用途，后续如重新录制或补拍，可按同类内容替换图片。

| ID | 目标内容 | 页面/命令 | 当前状态 | 拍摄/生成建议 | 说明 |
|---|---|---|---|---|---|
| UM-01 | Overview 截图 | Web Console Overview | 已补充 | 打开 `http://127.0.0.1:18081/?token=...`，显示 Overview、Host、Kernel、Git、Evidence | 展示 SP4 主验证 + SP3 强制兼容矩阵。 |
| UM-02 | Skills & Agent 截图 | Web Console Skills & Agent | 已补充 | 点击 Skills & Agent，运行只读刷新 | 显示 9 个 Skill；safe 状态下 available=false 不等于功能缺失。 |
| UM-03 | Evidence 截图 | Web Console Evidence | 已补充 | 打开 Evidence & Live Demo 的 Evidence 区 | 按评分项展示 evidence。 |
| UM-04 | Policy Timeline 截图 | Policy Engine Timeline | 已补充 | 打开 Policy Engine Timeline | 展示 transaction_id 串联链路。 |
| UM-05 | Live Demo 截图 | Live Demo | 已补充 | 打开 Recommended Demo 和 Advanced/Optional 分区 | 展示 token/确认/cleanup 风险提示。 |
| UM-06 | CLI dry-run 截图 | `./build/eulerpilot-agent --config configs/agent.yaml --duration-s 5` | 已补充 | 终端截图 | 展示 Agent 表格输出、mark legend、reason。 |
| UM-07 | doctor-safe 截图 | `./build/eulerpilot-agent --doctor-safe --config configs/agent.yaml` | 已补充 | 终端截图 | 展示 safe doctor 不加载探针。 |
| UM-08 | release evidence 截图 | `python3 scripts/collect_final_evidence.py --validate-release` | 已补充 | 终端截图 | 展示 entries/missing/warnings。 |

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
- 图片中的结论必须来自当前交付仓库，不得混入旧仓库页面。
- 如果后续替换截图，必须同步更新本文档中的状态和正文图片引用。
