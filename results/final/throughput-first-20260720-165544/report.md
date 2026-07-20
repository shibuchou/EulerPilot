# Throughput-first 批处理证据

- 结果目录：`/root/EulerPilot/results/final/throughput-first-20260720-165544`
- 轮数：`3`
- worker 数：`2`
- 单轮时长：`8s`

## 口径

- 本实验在结果目录内临时编译名为 `sysbench` 的轻量 CPU worker，用于命中现有 managed batch 分类规则；它不是外部 sysbench 包。
- `EULERPILOT_THROUGHPUT_FIRST=1` 为显式实验开关，默认关闭，普通 Agent 行为不变。
- `class_hits_batch/enqueue_batch/dispatch_batch/running_batch` 来自 scx stats，用于证明 batch 路径命中；性能结论仍以本目录 ops/sec 为准，并按 workload 边界解释。

## 平均结果

| label | runs | ops/s avg | applied avg | throughput profile hits | class_hits_batch | enqueue_batch | dispatch_batch | running_batch |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| default_batch | 3 | 204710400.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 |
| cgroup_throughput_first | 3 | 204715212.800 | 32.000 | 32.000 | 0.000 | 0.000 | 0.000 | 0.000 |
| scx_throughput_first | 3 | 45483622.400 | 32.000 | 32.000 | 5.000 | 5.000 | 0.000 | 33.000 |

## 验收点

- `cgroup_throughput_first` 的 Agent snapshot 必须出现 `throughput_profile`。
- `scx_throughput_first` 的 Agent snapshot 必须出现 `THROUGHPUT_BATCH`，并保存 scx stats。
