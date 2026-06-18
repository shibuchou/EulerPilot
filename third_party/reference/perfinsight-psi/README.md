# PerfInsight PSI 参考包

本目录保存从 Android `PerfInsight` 原型中整理出的 PSI 相关参考源码。

包含文件：

- `psi_pf.bpf.c`
- `psi_pf.c`
- `merge_latency_lessfor.bpf.c`
- `merge_latency_lessfor.c`
- `perfinsight.h`
- `tags1.yaml`

用途：

- 保留 Android 原型中的 PSI 观测与门控设计作为参考基线
- 支撑 EulerPilot 中 PSI 模块的复用与移植设计

重要边界：

- 这里的代码面向 Android 原型环境
- 不直接作为 EulerPilot 生产模块参与编译
- 必须按 openEuler / Linux 工具链和当前项目结构重新抽象实现

在 EulerPilot 中，最值得复用的部分不是单独的 PSI 观测，而是围绕下列概念构建的 PSI 门控逻辑：

- `threshold`
- `trigger_resource`
- `trigger_window`
- `trigger_index`
- `psi_handle_flag`
- `trigger_flag`
