# tools

作用：存放辅助工具源码，主要用于观测、调试和离线分析。

## 当前内容

- `workload_observer_dump.c`：workload observer 相关数据 dump 工具。
- `tcp_rate_probe.py`：Network QoS Benchmark 使用的最小 TCP 吞吐探针，只依赖 Python 标准库，用于 isolated veth 场景下测量 baseline/enforce 吞吐。

## 当前完成状态

- 该目录目前为辅助工具区，不是 Agent 主运行路径。
- 后续如果公共控制面需要独立调试工具，可以放在这里，例如：
  - target 解析检查工具
  - capability 探测 dump 工具
  - audit/journal 检查工具

## 维护规则

- 工具代码应保持可独立编译或在 Makefile 中有明确入口。
- 工具输出格式如果被报告或测试依赖，需要在 docs 中固定字段语义。
