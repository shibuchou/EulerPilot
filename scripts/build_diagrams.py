#!/usr/bin/env python3
"""Build editable draw.io and portable SVG from one deterministic layout model.

Structural conventions follow the local drawio-skill. No network, runtime
workload, BPF loading or AI image generation is involved.
"""
from __future__ import annotations

import html
import json
from pathlib import Path
import textwrap
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "docs/assets/figures"
FONT = "Microsoft YaHei, Noto Sans CJK SC, PingFang SC, Arial, sans-serif"
PALETTE = {
    "control": ("#EEF5FC", "#2E6DA4", "控制与处理"),
    "effect": ("#EAF6F2", "#238678", "执行与结果"),
    "gate": ("#FFF7E7", "#B27812", "条件与约束"),
    "error": ("#FFF0EE", "#B65B50", "拒绝与恢复"),
    "neutral": ("#F5F7FA", "#8595A5", "输入与材料"),
}


class Diagram:
    def __init__(self, slug, title, subtitle, height=820, note=""):
        self.slug, self.title, self.subtitle = slug, title, subtitle
        self.width, self.height, self.note = 1120, height, note
        self.nodes, self.edges, self.groups = [], [], []

    def node(self, key, title, body, x, y, role="control", w=300, h=116, group=None):
        lines = body.split("\n") if body else []
        h = max(h, 90 + max(0, len(lines) - 1) * 27) if lines else h
        self.nodes.append(dict(id=key, title=title, body=lines,
                               x=x, y=y, w=w, h=h, role=role, group=group))
        return self

    def group(self, key, title, x, y, w, h):
        self.groups.append(dict(id=key, title=title, x=x, y=y, w=w, h=h))
        return self

    def edge(self, source, target, label="", start=None, end=None, via=None, dashed=False, role="control", label_at=None, end_ratio=0.5):
        self.edges.append(dict(source=source, target=target, label=label, start=start,
                               end=end, via=via or [], dashed=dashed, role=role,
                               label_at=label_at, end_ratio=end_ratio))
        return self


def figures():
    ds = []
    d = Diagram("system-architecture", "EulerPilot v2 总体架构", "逻辑分层与进程边界 · 当前实现视图", 1060,
                "每个实例选择一个效果域；自动通道默认关闭，启用仍须显式策略与租约。")
    d.group("process", "ep-executord 同一进程内", 30, 275, 1060, 585)
    d.groups[-1]["label_x"] = 750
    d.node("cli", "ep CLI 与声明式策略", "Schema / 声明式策略信封\nUnix socket · 同 UID 校验", 410, 120, "neutral")
    d.node("host", "主机与服务观测", "CPU / load / PSI\nservice.observe", 50, 120, "neutral")
    d.node("pump", "ep-evtmon 事件泵", "独立进程 · 只读采集链\nevent.observe 上报", 770, 120, "neutral")
    d.node("route", "特征窗与规则路由", "有效性 / 新鲜度 / 滞回\n默认仅输出建议", 70, 340, group="process")
    d.node("gate", "SLO 门控与授权", "服务证据只能收紧\nAutoPilot / EventArm", 410, 340, "gate", group="process")
    d.node("owner", "所有权与事务引擎", "lease / fence / generation\n快照 · WAL · 幂等 · 恢复", 750, 340, group="process")
    d.node("sched", "sched 效果域", "SchedulingPolicy\nloader / cfg / class_map", 190, 560, "effect", w=330, group="process")
    d.node("cgroup", "cgroup 效果域", "CgroupPolicy / cpu.weight\nmarker / 事件守卫", 600, 560, "effect", w=330, group="process")
    d.node("journal", "写前日志与恢复记录", "journal.jsonl / state.json\nruntime / effects marker", 350, 720, "neutral", w=420, h=90, group="process")
    d.node("kernel", "openEuler SP3 内核", "sched_ext / cgroup v2 / raw tracepoints · 读回自证与观测反馈", 90, 870, "effect", w=940, h=90)
    d.edge("host", "route", start="S", end="N", via=[(200, 260), (220, 260)])
    d.edge("cli", "gate", start="S", end="N")
    d.edge("pump", "gate", start="S", end="N", via=[(920, 260), (560, 260)], dashed=True)
    d.edge("route", "gate").edge("gate", "owner")
    d.edge("owner", "sched", start="S", end="N", via=[(900, 500), (355, 500)], label="二选一", label_at=(555, 500))
    d.edge("owner", "cgroup", start="S", end="N", via=[(900, 520), (765, 520)])
    d.edge("sched", "kernel", start="S", end="N", via=[(355, 690), (100, 690), (100, 845), (300, 845)], role="effect", end_ratio=210/940)
    d.edge("cgroup", "kernel", start="S", end="N", via=[(765, 690), (1020, 690), (1020, 845), (820, 845)], role="effect", end_ratio=730/940)
    d.edge("owner", "journal", start="E", end="E", via=[(1070, 398), (1070, 765)], dashed=True)
    ds.append(d)

    d = Diagram("policy-apply-flow", "策略生效与补偿流程", "所有变更复用唯一执行服务的校验与事务路径", 910,
                "校验失败零副作用拒绝；生效或读回验证失败进入补偿，恢复失败保留现场。")
    for key, title, body, x, y, role in [
        ("submit", "01  CLI 提交", "envelope + policy\n幂等键与请求标识", 40, 130, "neutral"),
        ("auth", "02  校验与授权", "schema / 语义 / lease\nfence / generation / 门控", 410, 130, "gate"),
        ("bind", "03  解析目标身份", "pidfd + starttime\ncgroup FD / kernfs id", 780, 130, "control"),
        ("wal", "04  快照先行", "PREPARED + fsync\n保存补偿所需原值", 40, 345, "control"),
        ("apply", "05  Skill 生效", "sched 或 cgroup\n应用获准参数", 410, 345, "effect"),
        ("verify", "06  内核读回", "state / ops / enable_seq\nmap 或 cpu.weight", 780, 345, "gate"),
        ("rollback", "失败补偿", "撤销本实例效应\n再读回确认恢复", 40, 570, "error"),
        ("result", "08  返回 ActionResult", "实际状态 / generation\naction_id / snapshot_id", 410, 570, "effect"),
        ("commit", "07  提交与记账", "VERIFIED → COMMITTED\n持久化世代与回执", 780, 570, "control"),
    ]: d.node(key, title, body, x, y, role)
    d.edge("submit", "auth").edge("auth", "bind")
    d.edge("bind", "wal", start="S", end="N", via=[(930, 295), (190, 295)])
    d.edge("wal", "apply").edge("apply", "verify")
    d.edge("verify", "commit", start="S", end="N", label="通过")
    d.edge("commit", "result", start="W", end="E")
    d.edge("apply", "rollback", start="S", end="N", via=[(560, 515), (190, 515)], label="失败", role="error", label_at=(400, 515))
    d.node("blocked", "恢复未确认", "RECOVERY_REQUIRED\n禁止创建新效应", 40, 730, "error", h=80)
    d.edge("rollback", "blocked", start="S", end="N", label="失败", role="error")
    d.edge("rollback", "result", label="成功", role="effect")
    ds.append(d)

    d = Diagram("transaction-state-machine", "事务日志状态与恢复", "正常提交、显式回退与失败补偿分别留痕", 870,
                "RESULT 是动作回执；恢复效应与恢复记录分别确认，不把审计失败伪装成成功。")
    d.node("prepared", "PREPARED", "快照与意图先落盘", 40, 130)
    d.node("applying", "APPLYING", "执行已授权变更", 410, 130)
    d.node("verifying", "VERIFYING", "读回实际内核状态", 780, 130, "gate")
    d.node("compensating", "COMPENSATING", "失败后撤销效应\n验证是否已恢复", 40, 370, "error")
    d.node("committed", "COMMITTED", "验证通过并提交\n世代号随成功变更推进", 410, 370, "effect")
    d.node("verified", "VERIFIED", "内核状态与计划相符", 780, 370, "effect")
    d.node("recovery", "RECOVERY_REQUIRED", "恢复失败或存在冲突\n保留上下文并拒绝新建", 40, 610, "error")
    d.node("rolled", "ROLLED_BACK", "补偿完成且读回确认", 410, 610, "effect")
    d.node("manual", "ROLLBACK_PREPARED", "显式回退先记录\n引用原动作与快照", 780, 610)
    d.edge("prepared", "applying").edge("applying", "verifying")
    d.edge("verifying", "verified", start="S", end="N").edge("verified", "committed", start="W", end="E")
    d.edge("applying", "compensating", start="S", end="N", via=[(560, 305), (190, 305)], label="失败", role="error")
    d.edge("compensating", "recovery", start="S", end="N", label="未恢复", role="error")
    d.edge("compensating", "rolled", start="E", end="N", via=[(380, 428), (380, 555), (560, 555)], label="恢复", role="effect")
    d.edge("committed", "manual", start="S", end="N", via=[(560, 535), (930, 535)], label="回退")
    d.edge("manual", "rolled", start="W", end="E", role="effect")
    ds.append(d)

    d = Diagram("scheduler-data-path", "sched_ext 数据面与公平调度", "BASIC / CLASS 的实际路径 · 不将规划能力画成已启用功能", 1040,
                "LAT 份额意图由片长比表达；预算与 aging 是软件保护，不是硬实时保证。")
    d.node("policy", "有效策略与任务身份", "sp_cfg / class_map\ntask_ctx / 身份 cookie", 410, 120, "gate")
    d.node("select", "select_cpu / enqueue", "统一参数快照\n保留排队与运行上下文", 410, 310)
    d.node("local", "本机 local DSQ", "空闲直投 / 单 CPU 绑核\n必要时 SCX_KICK_IDLE", 40, 520, "effect")
    d.node("lat", "LAT 队列", "有界优先\n份额意图与 aging", 410, 520, "control")
    d.node("other", "非 LAT 队列", "CLASS：COMP / CACHE\nBG 进入 COMP\nBASIC 使用 SHARED", 780, 520, "control")
    d.node("dispatch", "dispatch 与运行记账", "预算 / 等待老化 / 轮转\n按实际 runtime 记账", 590, 740, "gate", w=380)
    d.node("cpu", "CPU 运行", "running / stopping\n任务级上下文与统计", 100, 740, "effect", w=340)
    d.edge("policy", "select", start="S", end="N")
    d.edge("select", "local", start="W", end="N", via=[(190, 368)], label="直投", role="effect")
    d.edge("select", "lat", start="S", end="N")
    d.edge("select", "other", start="E", end="N", via=[(930, 368)])
    d.edge("lat", "dispatch", start="S", end="N", via=[(560, 685), (780, 685)])
    d.edge("other", "dispatch", start="S", end="E", via=[(1030, 685), (1030, 798)])
    d.edge("dispatch", "cpu", start="W", end="E", role="effect")
    d.edge("local", "cpu", start="S", end="N", via=[(190, 690), (270, 690)], role="effect")
    ds.append(d)

    d = Diagram("event-driven-control-loop", "eBPF 事件驱动资源闭环", "感知端只上报事实 · 动作端复用同一事务通道", 870,
                "事件链证明功能闭环；不据此宣称已经测出调权后的业务 goodput 收益。")
    d.node("load", "目标 cgroup 与负载", "显式 watch 授权\npath + kernfs id", 40, 130, "neutral")
    d.node("bpf", "raw tracepoint BPF", "采集 runnable 等待\n按 cgroup 聚合", 410, 130)
    d.node("pump", "ep-evtmon", "读聚合 map / 排空窗口\n按周期上报观测", 780, 130)
    d.node("weight", "cpu.weight 生效", "读回验证 / 刷新纠漂\n原值 marker 可恢复", 40, 390, "effect")
    d.node("txn", "统一事务与唯一写者", "lease / WAL / verify\napply / rollback", 410, 390, "effect")
    d.node("guard", "EventArm 武装守卫", "新鲜度 / 身份 / 连续窗\n人工阈值与属主租约", 780, 390, "gate")
    d.node("restore", "条件回落或 TTL 到期", "经补偿恢复原值\n恢复结果再次读回", 40, 630, "effect")
    d.node("stale", "泵停止或窗口失效", "门冻结 / STALE\n已有效应由 TTL 兜底", 780, 630, "error")
    d.edge("load", "bpf").edge("bpf", "pump")
    d.edge("pump", "guard", start="S", end="N", label="上报", dashed=True)
    d.edge("guard", "txn", start="W", end="E", label="满足")
    d.edge("txn", "weight", start="W", end="E", role="effect")
    d.edge("weight", "load", start="W", end="W", via=[(20, 448), (20, 188)], label="反馈", dashed=True, role="effect", label_at=(55, 310))
    d.edge("weight", "restore", start="S", end="N", role="effect")
    d.edge("guard", "stale", start="S", end="N", label="断供", role="error")
    ds.append(d)

    d = Diagram("event-guard-decision", "事件守卫的判定与动作", "监控线程周期检查 · 观测不转授权限", 1080,
                "泵断供时冻结新动作；同路径重建不继承旧授权，已有效应仍受 TTL 约束。")
    d.node("window", "新窗口与新鲜度", "是否有新的有效证据\n过期不猜测负载状态", 410, 120, "gate")
    d.node("hold", "NONE / STALE", "不创建新效应\n记录 last_hold 原因", 40, 330, "error")
    d.node("identity", "watch 身份与租约", "path + kernfs id 一致\narm 属主与效应归属一致", 410, 330, "gate")
    d.node("rebuilt", "WATCH_REBUILT", "授权目标已改变\n回退并要求重新武装", 780, 330, "error")
    d.node("sustain", "尚无在位效应", "连续 sustain 窗超阈\n才允许提交新动作", 160, 550, "gate", w=340)
    d.node("release", "已有在位效应", "连续 release 窗回落\n或策略 TTL 到期", 620, 550, "gate", w=340)
    d.node("apply", "标准 policy.apply", "经快照 / WAL / verify\n受控提升 cpu.weight", 160, 780, "effect", w=340)
    d.node("rollback", "标准补偿回退", "恢复 previous\n读回后确认结果", 620, 780, "effect", w=340)
    d.edge("window", "hold", start="W", end="N", via=[(190, 178)], label="无效", role="error")
    d.edge("window", "identity", start="S", end="N", label="有效")
    d.edge("identity", "rebuilt", label="不符", role="error")
    d.edge("identity", "sustain", start="S", end="N", via=[(560, 500), (330, 500)])
    d.edge("identity", "release", start="S", end="N", via=[(560, 500), (790, 500)])
    d.edge("sustain", "apply", start="S", end="N", label="满足", role="effect")
    d.edge("release", "rollback", start="S", end="N", label="满足", role="effect")
    ds.append(d)

    d = Diagram("startup-recovery", "启动恢复与降级判定", "先对账与拆除无主效应，再接受新的策略动作", 990,
                "正常 shutdown 先补偿并验证再退出；崩溃后由同实例重启执行恢复。")
    d.node("lock", "独占状态目录", "flock / instance id\n拒绝并发写者", 40, 130, "gate")
    d.node("wal", "修复日志并对齐世代", "截断撕裂尾行\n对齐最大已提交世代", 410, 130)
    d.node("orphan", "recover_orphan", "核对 runtime / marker\n只处理本实例资源", 780, 130)
    d.node("sched", "sched 无主效应", "撤销受管 class 条目\n核对 pin 身份并回收", 160, 360, "effect", w=340)
    d.node("cg", "cgroup 无主效应", "按 marker 恢复原值\n确认目标身份与归属", 620, 360, "effect", w=340)
    d.node("reconcile", "逐事务 reconcile", "恢复未完成动作\n验证补偿与持久记录", 410, 580)
    d.node("ready", "可服务状态", "启动监控线程\n允许获准的新建效应", 160, 790, "effect", w=340)
    d.node("degraded", "DEGRADED", "冲突 / 恢复未确认\n保留现场，仅放行拆除", 620, 790, "error", w=340)
    d.edge("lock", "wal").edge("wal", "orphan")
    d.edge("orphan", "sched", start="S", end="N", via=[(930, 300), (330, 300)])
    d.edge("orphan", "cg", start="S", end="N", via=[(930, 300), (790, 300)])
    d.edge("sched", "reconcile", start="S", end="N", via=[(330, 530), (560, 530)])
    d.edge("cg", "reconcile", start="S", end="N", via=[(790, 530), (560, 530)])
    d.edge("reconcile", "ready", start="S", end="N", via=[(560, 745), (330, 745)], label="已恢复", role="effect")
    d.edge("reconcile", "degraded", start="S", end="N", via=[(560, 745), (790, 745)], label="未恢复", role="error")
    ds.append(d)

    d = Diagram("deployment-paths", "SP3 部署与构建路径", "交付镜像与源码重建两条入口，汇合到同一验证流程", 1040,
                "BPF 对象与用户态构建须匹配目标内核；libbpf 使用已核验的静态构建配方。")
    d.node("image", "入口 A 交付镜像", "qcow2 / domain.xml\nSHA256SUMS", 150, 130, "neutral", w=340)
    d.node("source", "入口 B 源码重建", "内核源 / BTF / 工具链\n可追溯源码与配置", 630, 130, "neutral", w=340)
    d.node("boot", "导入并启动 SP3", "核验启动参数\ncgroup v2 / PSI / sched_ext", 150, 345, "control", w=340)
    d.node("bpf", "先构建 BPF", "object → skeleton\n记录对象与工具链哈希", 630, 345, "control", w=340)
    d.node("doctor", "doctor 与包校验", "核验环境与冻结产物\n不以探测代替真实加载", 150, 560, "gate", w=340)
    d.node("user", "再构建用户态", "CMake + 静态 libbpf\nCLI / executor / loader / 泵", 630, 560, "control", w=340)
    d.node("verify", "按层验证后进入演示", "契约 / 单元 / 内核 / 验收 / 故障恢复\n确认 disabled、pin 与自有进程干净收尾", 150, 790, "effect", w=820, h=116)
    d.edge("image", "boot", start="S", end="N").edge("boot", "doctor", start="S", end="N")
    d.edge("source", "bpf", start="S", end="N").edge("bpf", "user", start="S", end="N")
    d.edge("user", "doctor", start="W", end="E", label="产物")
    d.edge("doctor", "verify", start="S", end="N", via=[(320, 730), (560, 730)], role="effect")
    ds.append(d)

    d = Diagram("verification-layers", "分层验证与严格验收", "不同层级回答不同问题，功能通过不等于性能提升", 1090,
                "严格验收拒绝 SKIP、缺失项和脏收尾；性能结论独立依赖冻结构建与正式矩阵。")
    for key, title, body, x, y, role in [
        ("contract", "L1 契约与语义", "schema / 正反例\n不加载内核", 40, 130, "control"),
        ("unit", "L2 单元与离线", "判定矩阵 / 身份 / 恢复\n可注入故障边界", 410, 130, "control"),
        ("kernel", "L3 SP3 内核集成", "真实 attach / 读回\ncgroup 域与事件链", 780, 130, "effect"),
        ("perf", "L6 正式性能", "同预算 / 配对 block\n原始件与置信区间", 40, 365, "effect"),
        ("fault", "L5 故障恢复", "失联 / 崩溃 / 冲突\nfail-safe 场景矩阵", 410, 365, "error"),
        ("accept", "L4 严格验收", "8 项 CTest 必须实跑\n禁止跳过与缺项", 780, 365, "gate"),
    ]: d.node(key, title, body, x, y, role)
    d.edge("contract", "unit").edge("unit", "kernel").edge("kernel", "accept", start="S", end="N")
    d.edge("accept", "fault", start="W", end="E").edge("fault", "perf", start="W", end="E")
    d.group("one", "一轮 accept_sp3.sh 的内部顺序", 30, 575, 1060, 335)
    for key, title, body, x, y in [
        ("idle", "01 idle 前置", "disabled / 空 pin\n无并发实例", 60, 640),
        ("ctest", "02 必测项 + liveness", "8 项无 SKIP\nA/B/C 均推进", 410, 640),
        ("hash", "03 产物溯源", "源码 / 二进制 / BPF\n哈希记录", 760, 640),
    ]: d.node(key, title, body, x, y, "gate", group="one")
    d.node("clean", "04 收尾断言", "state 归位 · 自有资源清理 · 失败保留现场", 250, 810, "effect", w=620, h=75, group="one")
    d.edge("idle", "ctest").edge("ctest", "hash")
    d.edge("hash", "clean", start="S", end="E", via=[(910, 847)])
    ds.append(d)

    d = Diagram("cgroup-effect-lifecycle", "cgroup 效应的完整生命周期", "快照、marker、读回与恢复形成闭环", 900,
                "域不匹配时零副作用拒绝；崩溃不会自动撤销权重，重启须按 marker 恢复。")
    d.node("target", "解析与校验目标", "CgroupPolicy / 授权路径\n持有并核对目标身份", 40, 130, "gate")
    d.node("snapshot", "保存 previous", "读取原值\n独立 marker 落盘", 410, 130)
    d.node("write", "写入并读回 desired", "cpu.weight\n验证实际值符合策略", 780, 130, "effect")
    d.node("restore", "显式回退与 TTL", "恢复 previous\n读回后清 marker", 40, 390, "effect")
    d.node("refresh", "刷新与漂移纠正", "检查在位目标\n外部改值后重新断言", 410, 390, "gate")
    d.node("crash", "执行服务被杀", "内核权重仍保留\nmarker 不丢失", 780, 390, "error")
    d.node("restart", "同实例重启恢复", "核对目标与归属\n恢复原值或明确冲突", 780, 640, "control")
    d.node("done", "确认效应已撤销", "读回原值\n清理自有恢复记录", 40, 640, "effect")
    d.edge("target", "snapshot").edge("snapshot", "write")
    d.edge("write", "refresh", start="S", end="N", via=[(930, 320), (560, 320)])
    d.edge("refresh", "restore", start="W", end="E", label="回退")
    d.edge("write", "crash", start="S", end="N", label="崩溃", role="error")
    d.edge("crash", "restart", start="S", end="N")
    d.edge("restore", "done", start="S", end="N", role="effect")
    d.edge("restart", "done", start="W", end="E", label="已恢复", role="effect")
    ds.append(d)

    d = Diagram("demo-timeline", "现场演示八步时间线", "demo_sp3.sh · 每一步同时展示动作与可验证结果", 930,
                "演示证明可操作与可恢复；正式性能数字从独立冻结证据读取，不用现场波动代替统计。")
    items = [
        ("a", "01 契约与服务", "检查 schema\n执行服务就绪", 40, 130),
        ("b", "02 默认 fair 基线", "未挂载 scx\n观察服务可运行", 410, 130),
        ("c", "03 策略生效", "ops / class 读回\n内核自证", 780, 130),
        ("f", "06 cgroup 调权", "写入与读回\n恢复记录在案", 40, 365),
        ("e", "05 崩溃与恢复", "SIGKILL / 重启\n检查补偿与回收", 410, 365),
        ("d", "04 感知链应答", "features / route / slo\n扰动下实时查询", 780, 365),
        ("g", "07 事件驱动防护", "观测 → 升权\n清除条件 → 补偿", 225, 605),
        ("h", "08 干净收尾", "disabled / 无自有 pin\n输出 PASS 或现场路径", 595, 605),
    ]
    for key, title, body, x, y in items: d.node(key, title, body, x, y, "effect" if key in "cfh" else "control")
    d.edge("a", "b").edge("b", "c").edge("c", "d", start="S", end="N")
    d.edge("d", "e", start="W", end="E").edge("e", "f", start="W", end="E")
    d.edge("f", "g", start="S", end="N", via=[(190, 550), (375, 550)])
    d.edge("g", "h")
    ds.append(d)

    d = Diagram("evidence-provenance", "构建与实验证据溯源", "明确区分性能所用构建与最终交付构建", 970,
                "文档插图更新不改变运行二进制；性能结论仍归属对应冻结构建，不向新构建静默转移。")
    d.node("source", "源码与环境输入", "commit / config / BTF\n编译器与依赖版本", 40, 130, "neutral")
    d.node("build", "可追溯构建", "BPF object / skeleton\n用户态二进制", 410, 130)
    d.node("hash", "冻结与哈希清单", "完整 SHA256\nPROVENANCE / manifest", 780, 130, "gate")
    d.node("gen1", "gen-1 性能构建", "3693c9ac…\n正式主矩阵与补充臂", 160, 360, "control", w=340)
    d.node("gen3", "gen-3 最终交付", "6f667b2e…\n功能 / 故障 / 演示回归", 620, 360, "control", w=340)
    d.node("raw", "性能原始记录", "逐请求 / block / attempts\n随机种子与无效原因", 160, 580, "neutral", w=340)
    d.node("accept", "交付验收记录", "CTest / 严格验收\nfail-safe / 演示 / 包校验", 620, 580, "neutral", w=340)
    d.node("report", "可重算报告与交付包", "对照、置信区间、失败与边界同时披露\n不将完整性检查等同于性能结论", 160, 790, "effect", w=800, h=90)
    d.edge("source", "build").edge("build", "hash")
    d.edge("hash", "gen1", start="S", end="N", via=[(930, 300), (330, 300)])
    d.edge("hash", "gen3", start="S", end="N", via=[(930, 300), (790, 300)])
    d.edge("gen1", "raw", start="S", end="N").edge("gen3", "accept", start="S", end="N")
    d.edge("raw", "report", start="S", end="N", via=[(330, 745), (560, 745)])
    d.edge("accept", "report", start="S", end="N", via=[(790, 745), (560, 745)])
    ds.append(d)

    d = Diagram("experiment-design", "同预算的配对实验设计", "先 pilot 后冻结 · 实验臂共用环境、负载与授权角色", 1040,
                "Liveness 只证明任务推进；性能需同时报告 goodput、尾延迟、后台进度和不确定性。")
    d.group("machine", "SP3 实验机 · 同一内核、同一应用配置", 30, 115, 1060, 345)
    d.node("client", "独立客户端 CPU 池", "请求发生器\n检查客户端未饱和", 70, 200, "neutral", w=300, group="machine")
    d.node("service", "服务 CPU 池", "目标服务\n逐请求延迟与完成数", 430, 200, "effect", w=300, group="machine")
    d.node("bg", "同池后台干扰", "显式 BACKGROUND\n可计数的工作进度", 790, 200, "neutral", w=260, group="machine")
    d.edge("client", "service", label="请求").edge("bg", "service", start="W", end="E", dashed=True, label="竞争")
    d.node("freeze", "冻结协议与构建", "机器 / 预算 / SLO / 种子\n参数与无效轮次规则", 40, 530, "gate")
    d.node("block", "平衡顺序的配对 block", "A0 / A1 / A2 / C / D\nB、E 作为补充对照", 410, 530)
    d.node("analysis", "原始件重算", "逐 block 配对\nbootstrap 置信区间", 780, 530)
    d.node("metrics", "并列评估收益与代价", "goodput@SLO / p99 / 错误率\nBG 相对进度 / quiet 成本 / 失败率", 200, 790, "effect", w=720, h=105)
    d.edge("freeze", "block").edge("block", "analysis")
    d.edge("analysis", "metrics", start="S", end="E", via=[(930, 735), (1020, 735), (1020, 842)], role="effect")
    ds.append(d)

    d = Diagram("bounded-autopilot", "自动切换的受限决策通道", "默认 advisory · 显式开启后仍不获得额外权限", 1050,
                "服务与事件观测只会让门更严；证据不足、分辨率不足或租约不符时保持不动作。")
    d.node("features", "FeatureWindow", "CPU / load / PSI\nvalid / age / source", 40, 130, "neutral")
    d.node("router", "规则专家路由", "阈值与滞回\n输出档位及理由", 410, 130)
    d.node("advisory", "默认建议模式", "route / slo 可查询\n不自动写内核", 780, 130, "effect")
    d.node("enable", "显式开启与租约", "auto-policy / auto-owner\n权限来源保持不变", 40, 390, "gate")
    d.node("evidence", "服务与事件证据门", "新鲜度 / 绝对预算\n比较判定的分辨率护栏", 410, 390, "gate")
    d.node("suppress", "三层切换抑制", "最小驻留 / 候选保持\n窗口内切换预算", 780, 390, "gate")
    d.node("hold", "保持原状态", "缺证据 / 不满足条件\n记录 hold 原因", 410, 650, "error")
    d.node("apply", "复用标准事务", "apply → verify\nlease / WAL / 补偿", 780, 650, "effect")
    d.edge("features", "router").edge("router", "advisory", label="默认")
    d.edge("router", "enable", start="S", end="N", via=[(560, 315), (190, 315)], dashed=True, label="开启")
    d.edge("enable", "evidence").edge("evidence", "suppress")
    d.edge("evidence", "hold", start="S", end="N", label="拒绝", role="error")
    d.edge("suppress", "apply", start="S", end="N", label="满足", role="effect")
    ds.append(d)
    return ds


def anchor(node, side, ratio=0.5):
    x, y, w, h = (node[k] for k in ("x", "y", "w", "h"))
    return {"N": (x + w * ratio, y), "S": (x + w * ratio, y + h),
            "W": (x, y + h * ratio), "E": (x + w, y + h * ratio)}[side]


def edge_points(d, edge):
    ns = {n["id"]: n for n in d.nodes}
    a, b = ns[edge["source"]], ns[edge["target"]]
    dx = b["x"] + b["w"] / 2 - a["x"] - a["w"] / 2
    dy = b["y"] + b["h"] / 2 - a["y"] - a["h"] / 2
    start = edge["start"] or ("E" if dx > 0 else "W") if abs(dx) >= abs(dy) else edge["start"] or ("S" if dy > 0 else "N")
    end = edge["end"] or {"E": "W", "W": "E", "N": "S", "S": "N"}[start]
    p, q = anchor(a, start), anchor(b, end, edge.get("end_ratio", 0.5))
    via = list(edge["via"])
    if not via and p[0] != q[0] and p[1] != q[1]:
        if start in ("E", "W"):
            mid = (p[0] + q[0]) / 2
            via = [(mid, p[1]), (mid, q[1])]
        else:
            mid = (p[1] + q[1]) / 2
            via = [(p[0], mid), (q[0], mid)]
    points = [p, *via]
    last = points[-1]
    if last[0] != q[0] and last[1] != q[1]:
        points.append((q[0], last[1]) if end in ("N", "S") else (last[0], q[1]))
    return [*points, q], start, end


def drawio(d):
    root = ET.Element("mxfile", host="EulerPilot", version="26.0.0")
    diagram = ET.SubElement(root, "diagram", id=d.slug, name=d.title)
    model = ET.SubElement(diagram, "mxGraphModel", dx="1120", dy=str(d.height), grid="1",
                          gridSize="10", page="1", pageWidth="1120", pageHeight=str(d.height), background="#FFFFFF")
    cells = ET.SubElement(model, "root")
    ET.SubElement(cells, "mxCell", id="0")
    ET.SubElement(cells, "mxCell", id="1", parent="0")
    groups = {g["id"]: g for g in d.groups}
    for g in d.groups:
        cell = ET.SubElement(cells, "mxCell", id="g_" + g["id"], value=g["title"], vertex="1", parent="1",
                             style="swimlane;startSize=38;container=1;pointerEvents=0;collapsible=0;html=1;rounded=1;fillColor=#F8FAFC;strokeColor=#CBD5E1;fontFamily=Microsoft YaHei;fontSize=22;fontColor=#475569;")
        ET.SubElement(cell, "mxGeometry", x=str(g["x"]), y=str(g["y"]), width=str(g["w"]), height=str(g["h"]), **{"as": "geometry"})
    for n in d.nodes:
        fill, stroke, _ = PALETTE[n["role"]]
        group = groups.get(n["group"])
        value = "<b>" + html.escape(n["title"]) + "</b>" + "".join("<br>" + html.escape(t) for t in n["body"])
        cell = ET.SubElement(cells, "mxCell", id="n_" + n["id"], value=value, vertex="1",
                             parent="g_" + n["group"] if group else "1",
                             style=f"rounded=1;arcSize=12;whiteSpace=wrap;html=1;fillColor={fill};strokeColor={stroke};strokeWidth=1.6;fontFamily=Microsoft YaHei;fontSize=22;fontColor=#17324D;spacing=12;align=left;verticalAlign=middle;")
        ET.SubElement(cell, "mxGeometry", x=str(n["x"] - (group["x"] if group else 0)), y=str(n["y"] - (group["y"] if group else 0)),
                      width=str(n["w"]), height=str(n["h"]), **{"as": "geometry"})
    pins = {"N": (0.5, 0), "S": (0.5, 1), "W": (0, 0.5), "E": (1, 0.5)}
    for i, edge in enumerate(d.edges):
        pts, start, end = edge_points(d, edge)
        sx, sy = pins[start]; tx, ty = pins[end]
        if end in ("N", "S"): tx = edge.get("end_ratio", 0.5)
        else: ty = edge.get("end_ratio", 0.5)
        stroke = PALETTE[edge["role"]][1]
        style = f"edgeStyle=orthogonalEdgeStyle;rounded=1;orthogonalLoop=1;jettySize=auto;html=1;endArrow=block;endFill=1;endSize=8;strokeWidth=2;strokeColor={stroke};fontFamily=Microsoft YaHei;fontSize=20;labelBackgroundColor=#FFFFFF;exitX={sx};exitY={sy};entryX={tx};entryY={ty};"
        if edge["dashed"]: style += "dashed=1;"
        cell = ET.SubElement(cells, "mxCell", id=f"e_{i}", value=edge["label"], edge="1", parent="1", source="n_" + edge["source"], target="n_" + edge["target"], style=style)
        geometry = ET.SubElement(cell, "mxGeometry", relative="1", **{"as": "geometry"})
        if pts[1:-1]:
            points = ET.SubElement(geometry, "Array", **{"as": "points"})
            for x, y in pts[1:-1]: ET.SubElement(points, "mxPoint", x=str(x), y=str(y))
    for key, label, y, size in (("title", d.title, 22, 30), ("subtitle", d.subtitle, 65, 21), ("note", d.note, d.height - 55, 20)):
        cell = ET.SubElement(cells, "mxCell", id="text_" + key, value=label, vertex="1", parent="1",
                             style=f"text;html=1;align=left;whiteSpace=wrap;fontFamily=Microsoft YaHei;fontSize={size};fontColor=#17324D;")
        ET.SubElement(cell, "mxGeometry", x="40", y=str(y), width="1040",
                      height="38" if key == "title" else "30" if key == "subtitle" else "45",
                      **{"as": "geometry"})
    return ET.tostring(root, encoding="unicode")


def svg(d, xml):
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{d.width}" height="{d.height}" viewBox="0 0 {d.width} {d.height}" role="img" aria-labelledby="title desc" content="{html.escape(xml, quote=True)}">',
             f'<title id="title">{html.escape(d.title)}</title><desc id="desc">{html.escape(d.subtitle + "。" + d.note)}</desc>',
             '<defs>' + ''.join(f'<marker id="arrow-{role}" markerWidth="10" markerHeight="10" refX="9" refY="5" orient="auto" markerUnits="userSpaceOnUse"><path d="M0,0 L10,5 L0,10 Z" fill="{color[1]}"/></marker>' for role, color in PALETTE.items()) + '</defs>',
             f'<style>text{{font-family:{FONT};fill:#17324D}}.title{{font-size:30px;font-weight:700}}.sub{{font-size:21px;fill:#64748B}}.nt{{font-size:24px;font-weight:600}}.nb{{font-size:22px}}.edge-label{{font-size:20px}}.footer{{font-size:20px;fill:#475569}}</style>',
             f'<rect width="{d.width}" height="{d.height}" fill="#FFFFFF"/>',
             f'<text class="title" x="40" y="48">{html.escape(d.title)}</text>',
             f'<text class="sub" x="40" y="84">{html.escape(d.subtitle)}</text>']
    for g in d.groups:
        parts.extend([f'<rect x="{g["x"]}" y="{g["y"]}" width="{g["w"]}" height="{g["h"]}" rx="12" fill="#F8FAFC" stroke="#CBD5E1" stroke-width="1.4"/>',
                      f'<text class="sub" x="{g.get("label_x", g["x"] + 18)}" y="{g["y"] + 28}">{html.escape(g["title"])}</text>'])
    labels = []
    for i, edge in enumerate(d.edges):
        pts, _, end = edge_points(d, edge)
        # Leave a small gap before the destination border rather than piercing it.
        dx, dy = {"N": (0, -5), "S": (0, 5), "W": (-5, 0), "E": (5, 0)}[end]
        pts[-1] = (pts[-1][0] + dx, pts[-1][1] + dy)
        path = "M " + " L ".join(f"{x:g},{y:g}" for x, y in pts)
        stroke = PALETTE[edge["role"]][1]
        dashed = ' stroke-dasharray="7 5"' if edge["dashed"] else ""
        parts.append(f'<path data-edge="{i}" d="{path}" fill="none" stroke="{stroke}" stroke-width="2.1" stroke-linejoin="round" marker-end="url(#arrow-{edge["role"]})"{dashed}/>')
        if edge["label"]:
            if edge["label_at"]:
                x, y = edge["label_at"]
            else:
                lengths = [abs(b[0]-a[0]) + abs(b[1]-a[1]) for a, b in zip(pts, pts[1:])]
                remaining = sum(lengths) / 2
                for a, b, length in zip(pts, pts[1:], lengths):
                    if remaining <= length and length:
                        f = remaining / length; x = a[0] + f*(b[0]-a[0]); y = a[1] + f*(b[1]-a[1]); break
                    remaining -= length
            labels.append((x, y, edge["label"]))
    for n in d.nodes:
        fill, stroke, _ = PALETTE[n["role"]]
        parts.append(f'<g data-node="{n["id"]}" data-box="{n["x"]},{n["y"]},{n["w"]},{n["h"]}"><rect x="{n["x"]}" y="{n["y"]}" width="{n["w"]}" height="{n["h"]}" rx="10" fill="{fill}" stroke="{stroke}" stroke-width="1.6"/>')
        title_y = n["y"] + (35 if n["body"] else n["h"] / 2 + 8)
        parts.append(f'<text class="nt" x="{n["x"]+20}" y="{title_y:g}">{html.escape(n["title"])}</text>')
        for i, line in enumerate(n["body"]):
            parts.append(f'<text class="nb" x="{n["x"]+20}" y="{n["y"]+66+i*27}">{html.escape(line)}</text>')
        parts.append('</g>')
    for x, y, label in labels:
        width = max(48, len(label) * 21 + 14)
        parts.extend([f'<rect x="{x-width/2:g}" y="{y-14:g}" width="{width}" height="27" rx="4" fill="#FFFFFF"/>',
                      f'<text class="edge-label" text-anchor="middle" x="{x:g}" y="{y+7:g}">{html.escape(label)}</text>'])
    roles = list(dict.fromkeys(n["role"] for n in d.nodes))
    if len(roles) >= 3:
        x = 40
        for role in roles:
            fill, stroke, label = PALETTE[role]
            parts.extend([f'<rect x="{x}" y="{d.height-112}" width="22" height="16" rx="3" fill="{fill}" stroke="{stroke}"/>',
                          f'<text class="footer" x="{x+32}" y="{d.height-97}">{label}</text>'])
            x += 200
    # CJK notes are wrapped by glyph count; the browser QA checks actual bounds.
    for i, line in enumerate(textwrap.wrap(d.note, 49, break_long_words=True, break_on_hyphens=False)):
        parts.append(f'<text class="footer" x="40" y="{d.height-60+i*26}">{html.escape(line)}</text>')
    parts.append('</svg>')
    return "\n".join(parts) + "\n"


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    models = []
    for d in figures():
        d.height = max(d.height, max(n["y"] + n["h"] for n in d.nodes) + 160)
        xml = drawio(d)
        (OUT / (d.slug + ".drawio")).write_text(xml + "\n", encoding="utf-8")
        (OUT / (d.slug + ".svg")).write_text(svg(d, xml), encoding="utf-8")
        models.append(dict(slug=d.slug, title=d.title, subtitle=d.subtitle, width=d.width,
                           height=d.height, note=d.note, nodes=d.nodes, edges=d.edges, groups=d.groups))
    (OUT / "diagrams.json").write_text(json.dumps(models, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Generated {len(models)} SVG and {len(models)} editable draw.io files in {OUT}")


if __name__ == "__main__":
    main()
