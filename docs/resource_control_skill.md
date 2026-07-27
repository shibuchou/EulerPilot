# ResourceControlSkill 说明

本文说明 EulerPilot Resource Control Agent 的当前能力、配置方式、事务化执行流程和验收入口。当前阶段已从早期 `cpu.weight + cgroup.procs` 扩展为 CPU + Memory + IO 自动闭环，并补齐真实 container runtime / Kubernetes lab Pod target 的可演示入口。

## 能力定位

`resource_control` 是 EulerPilot 的 cgroup v2 执行 Skill。它接收 workload 分类结果与 PSI gate 状态，在用户态做策略决策，并把动作落到 `/sys/fs/cgroup/eulerpilot/{latency,batch,background}` 三个实验 cgroup。

当前已落地能力：

- CPU：`cpu.weight`、`cpu.max`
- Memory：`memory.high`、`memory.low`、`memory.max`
- IO：`io.weight`、`io.max`
- Target：`target_ref` 可解析 cgroup、PID、container ID、runtime container name 和 Kubernetes Pod cgroup
- Real target harness：真实 Podman 容器 target 与真实 k3s Kubernetes Pod target 已在 121/122 转 pass；脚本默认不自动安装软件、拉镜像或创建集群资源，只有显式设置环境变量时才创建 lab Pod
- 可选 Memory reclaim：`memory.reclaim` 已有配置开关，默认关闭，后续只在明确 pressure 策略中启用
- 自动模式：`GateState::Active/Cooldown` 或非 `normal_profile` 时进入 pressure 模式
- 事务化执行：读取旧值、校验新值、写入控制器、复读验证、写 `AuditBus`、写 `ActionJournal`、停止时恢复旧值
- CPU quota 效果证据：使用 `cpu.stat usage_usec`、`nr_throttled`、`throttled_usec` 对比不限额和 `cpu.max=10000 100000` 窗口，验证实际 CPU 使用率下降和 throttling 生效；Redis compare benchmark 进一步拆分 `default_noisy`、`eulerpilot_no_quota` 和 `eulerpilot_quota`，避免混淆 Agent 放置影响与 quota 影响
- cpuset 口径：当前 release 明确采用安全降级方案，默认不启用动态 cpuset topology，不把 `cpuset.cpus/cpuset.mems` 计入封版完整功能和性能主结论。`scripts/setup_cgroup_v2.sh` 只有在显式设置 `EULERPILOT_ENABLE_CPUSET=1` 时才尝试实验性 cpuset 分组。

当前安全边界：

- 未配置 `target_ref` 时，只操作 `/sys/fs/cgroup/eulerpilot/latency`、`batch`、`background` 三个实验 cgroup，并把命中 workload 迁入对应 profile cgroup。
- 配置 `target_ref` 时，先通过 `TargetResolver` 解析真实 cgroup，再只对命中该 cgroup 的 workload 写控制器；不命中的进程会被标记为 `target-scope-mismatch`，不会被误限流。
- 默认不会修改系统 root cgroup；显式 `target_ref` 只应指向实验 cgroup、容器 cgroup 或比赛验证环境中明确允许管理的 Pod cgroup。
- `latency` 组默认不做 CPU quota 强限，只通过 `memory.low` 做保护。
- `background` 组在 pressure 模式下使用 `cpu.max`、`memory.high` 和 `io.max` 做限额。
- `memory.max` 默认保持 `max`，避免误杀实验进程。
- `io.max` 默认解析根文件系统所在块设备；121/122 当前均为 `253:0`。
- 真实 runtime / Pod 演示脚本默认不改变系统 root cgroup；容器脚本只操作 runtime 创建出的目标容器 cgroup，Pod 脚本只操作 `eulerpilot-lab` demo Pod cgroup，并在 Agent 退出或脚本清理时恢复旧值。

## YAML 配置

默认配置在 `configs/skills.yaml` 的 `resource_control.config` 下：

```yaml
mode: enforce
controllers:
  cpu:
    max:
      enabled: true
  memory:
    enabled: true
    high:
      enabled: true
    low:
      enabled: true
    max:
      enabled: true
    reclaim:
      enabled: false
  io:
    enabled: true
    device: auto
    weight:
      enabled: true
    max:
      enabled: true
targets: {}
profiles:
  latency:
    cpu_max: max
    memory_low: '67108864'
    memory_high: max
    memory_max: max
    io_weight: 'default 100'
    io_max: ''
  batch:
    cpu_max: max
    memory_low: '0'
    memory_high: max
    memory_max: max
    io_weight: 'default 100'
    io_max: ''
  background:
    normal:
      cpu_max: max
      memory_high: max
      io_weight: 'default 100'
      io_max: ''
    pressure:
      cpu_max: '20000 100000'
      memory_high: '134217728'
      memory_low: '0'
      memory_max: max
      memory_reclaim: ''
      io_weight: 'default 50'
      io_max: 'auto rbps=max wbps=1048576'
```

说明：

- `mode: enforce` 表示允许写 cgroup 控制器；如果 Agent 以 dry-run 启动，代码仍不会写入。
- `targets: {}` 是默认空 target 集合；需要按容器或 Pod 管控时，在这里声明 target，再在 `profiles.<name>.target_ref` 中引用。
- 测试脚本会把 pressure 策略收紧为 `cpu.max=10000 100000`、`memory.high=1048576`，便于在短时间内验证闭环。
- `controllers.memory.reclaim.enabled` 默认关闭，避免每个控制周期重复触发 one-shot reclaim。
- `controllers.io.device=auto` 会解析 `/` 所在块设备；IO 集成测试固定使用 `253:0`。

### Target 示例

直接管理已存在 cgroup：

```yaml
targets:
  background_scope:
    type: cgroup
    path: /sys/fs/cgroup/eulerpilot/target-background
profiles:
  background:
    target_ref: background_scope
```

后续接入真实容器或 Pod 时，配置形式保持一致：

```yaml
targets:
  build_container:
    type: container
    container_name: build-worker
    runtime: auto
  web_pod:
    type: k8s_pod
    namespace: eulerpilot-lab
    pod_name: web-demo
    container_name: nginx
profiles:
  background:
    target_ref: build_container
```

当前实现对 `type: container_id/container/k8s_pod/pid/cgroup` 统一解析为 cgroup path，再复用同一套 CPU/Memory/IO 事务化写入与 rollback。

## 执行流程

每次 cgroup 控制器写入遵循同一流程：

```text
select profile
  -> build CPU/Memory/IO desired values
  -> resolve optional target_ref
  -> skip samples outside target scope
  -> read old cgroup file value
  -> validate desired value
  -> write cgroup file
  -> read back and verify
  -> append reports/events/resource_control.jsonl
  -> append run/eulerpilot/action_journal.jsonl
  -> restore old values on stop/rollback
```

`cpu.max` 的验证包含语义匹配：写入 `max` 后内核可能读回 `max 100000`，代码会把它视为同一含义。

## 验收入口

最新通过结果：

- CPU+Memory 回归 121：`results/resource_control/integration-20260624-160317/summary.txt`
- CPU+Memory 回归 122：`results/resource_control/integration-20260624-160349/summary.txt`
- IO controller 121：`results/resource_control/io-20260624-160008/summary.txt`
- IO controller 122：`results/resource_control/io-20260624-160208/summary.txt`
- target_ref 改动后 IO 回归 121：`results/resource_control/io-regression-20260624-174400/summary.txt`
- target_ref cgroup 闭环 121：`results/resource_control/target-20260624-172139/summary.txt`
- target_ref cgroup 闭环 122：`results/resource_control/target-20260624-172916/summary.txt`
- runtime target 闭环 121：`results/resource_control/runtime-target-20260624-212403/summary.txt`；v3.2 回归：`results/resource_control/runtime-target-20260630-113310/summary.txt`
- runtime target 闭环 122：`results/resource_control/runtime-target-20260624-212529/summary.txt`；v3.2 回归：`results/resource_control/runtime-target-20260630-113354/summary.txt`
- runtime readiness 诊断 121：`results/resource_control/runtime-readiness-20260630-k3s-121/summary.txt`
- runtime readiness 诊断 122：`results/resource_control/runtime-readiness-20260630-k3s-122/summary.txt`
- real runtime target Podman 121：`results/resource_control/real-runtime-target-20260630-podman-121-final2/summary.txt`
- real runtime target Podman 122：`results/resource_control/real-runtime-target-20260630-podman-122-final2/summary.txt`
- real Pod target 121：`results/resource_control/real-pod-target-20260630-k3s-121-v2/summary.txt`
- real Pod target 122：`results/resource_control/real-pod-target-20260630-k3s-122-v1/summary.txt`
- CPU quota 效果 121：`results/resource_control/cpu-quota-20260625-095030/summary.txt`
- CPU quota 效果 122：`results/resource_control/cpu-quota-20260625-095114/summary.txt`
- Redis quota Compare Benchmark 121：`results/resource_control/redis-quota-compare-20260625-102426/summary.txt`
- Redis quota Compare Benchmark 122：`results/resource_control/redis-quota-compare-20260625-102611/summary.txt`
- Redis quota Sweep Benchmark 121：`results/resource_control/redis-quota-sweep-20260626-203131/summary.txt`
- Redis quota Sweep Benchmark 122：`results/resource_control/redis-quota-sweep-20260626-203505/summary.txt`
- Nginx quota Sweep Benchmark 121：`results/resource_control/nginx-quota-sweep-20260626-210702/summary.txt`
- Nginx quota Sweep Benchmark 122：`results/resource_control/nginx-quota-sweep-20260626-211057/summary.txt`
- Mixed Redis+Nginx quota Sweep Benchmark 121：`results/resource_control/mixed-quota-sweep-20260627-102503/summary.txt`
- Mixed Redis+Nginx quota Sweep Benchmark 122：`results/resource_control/mixed-quota-sweep-20260627-103139/summary.txt`
- Mixed Redis+Nginx Multi-Resource Benchmark 121：`results/resource_control/mixed-multi-resource-20260628-211631/summary.txt`
- Mixed Redis+Nginx Multi-Resource Benchmark 122：`results/resource_control/mixed-multi-resource-20260628-212132/summary.txt`
- Policy Engine Security -> Resource Control 联动 121：`results/policy_engine/security-resource-20260629-163949/summary.txt`
- Policy Engine Security -> Resource Control 联动 122：`results/policy_engine/security-resource-20260629-164135/summary.txt`

测试命令：

```bash
cd /root/EulerPilot
tests/integration/test_resource_control.sh
tests/integration/test_resource_control_io.sh
tests/integration/test_resource_control_target.sh
tests/integration/test_resource_control_runtime_target.sh
tests/integration/test_resource_control_runtime_readiness.sh
tests/integration/test_resource_control_real_runtime_target.sh
tests/integration/test_resource_control_real_pod_target.sh
tests/integration/test_resource_control_cpu_quota.sh
tests/benchmark/test_resource_control_redis_quota.sh
tests/benchmark/test_resource_control_redis_quota_compare.sh
tests/benchmark/test_resource_control_redis_quota_sweep.sh
tests/benchmark/test_resource_control_nginx_quota_sweep.sh
tests/benchmark/test_resource_control_mixed_quota_sweep.sh
tests/benchmark/test_resource_control_mixed_multi_resource.sh
tests/integration/test_policy_engine_security_resource.sh
```

测试覆盖：

- 构建 `eulerpilot-agent`
- 初始化 cgroup v2 CPU/Memory/IO controller
- 启动 `yes` 作为 background workload
- 以 `always-active + --active` 启动 Agent
- 验证 background 组写入 `cpu.max=10000 100000`
- 验证 background 组写入 `memory.high=1048576`
- 触发内存压力并验证 `memory.events high` 计数增加
- 验证 Agent 停止后 `cpu.max` 和 `memory.high` 恢复旧值
- 验证 `resource_control_events.jsonl` 包含 `cpu.max`、`memory.high` 的 `applied` 与 rollback 事件
- IO 测试验证 background 组写入 `io.max=253:0 rbps=max wbps=1048576` 与 `io.weight=default 50`
- IO 测试使用 direct write 验证 `io.stat wbytes` 增长和限速耗时增加
- IO 测试验证 Agent 停止后 `io.max` 和 `io.weight` 恢复旧值
- IO 测试验证 `resource_control_events.jsonl` 包含 `io.max`、`io.weight` 的 `applied` 与 `restored` 事件
- Target 测试验证 `profiles.background.target_ref` 能解析到指定 cgroup，只对目标 cgroup 写 `cpu.max/memory.high`，非目标 cgroup 保持原值
- Target 测试验证 Agent JSONL 和 `resource_control_events.jsonl` 都携带 `target_ref` 与目标 cgroup path，并在退出后恢复旧值
- Runtime target 测试验证 `container_id`、runtime container name 和 `k8s_pod` 名称解析均能落到目标 cgroup，只对目标 cgroup 写 `cpu.max/memory.high`，scope 外 cgroup 保持原值
- Runtime target 测试使用 fake `crictl/kubectl` 固定解析路径；v3.2 起 TargetResolver 额外支持 openEuler 常见 `iSulad/isula` runtime，不依赖真实容器服务；真实 Podman container 与 k3s lab Pod 已作为现场演示项在 121/122 双机转 pass
- Runtime readiness 诊断只读检查 docker/podman/isula/nerdctl/ctr/crictl/kubectl、systemd 服务、runtime socket、runtime cgroup 和 Kubernetes lab namespace；当前 121/122 均已通过 Podman + k3s 变为 `container_runtime_ready=1`、`kubernetes_ready=1`
- Real runtime target 脚本在 docker/podman/iSulad 与本地镜像可用时启动 CPU workload 容器，通过 `type: container + container_name + runtime` 配置 `target_ref`，验证容器 cgroup 上 `cpu.max=10000 100000`、`memory.high=1048576` 的 applied/restored 事件；当前 121/122 已使用 Podman 与本地 `localhost/eulerpilot-busybox:latest` 镜像通过
- Real Pod target 脚本在 `kubectl` 与 `eulerpilot-lab` demo Pod 可用时通过 `type: k8s_pod + namespace + pod_name` 配置 `target_ref`，验证 Pod cgroup 上 CPU/Memory 控制器写入、审计和 rollback；默认只使用已有 Pod，只有显式设置 `EULERPILOT_ALLOW_K8S_CREATE=1` 才创建 demo Pod；当前 121/122 k3s lab 均为 `result=pass`
- CPU quota 测试先在 `cpu.max=max` 下采样 CPU hog 的 `cpu.stat usage_usec`，再由 Agent 写入 `cpu.max=10000 100000` 后采样同一指标；测试要求 `usage_rate_ratio < 0.70`，且限额窗口 `nr_throttled/throttled_usec` 均增加
- Redis quota Compare Benchmark 在 Redis GET/SET 压测与 background CPU hog 同时运行时记录业务 RPS 和 background cgroup `cpu.stat`；它包含 `default_noisy`、`eulerpilot_no_quota` 和 `eulerpilot_quota` 三阶段，当前通过线聚焦同样 Agent 放置下后台限额是否生效，Redis RPS 作为业务侧证据记录，不包装成性能提升结论
- Redis quota Sweep Benchmark 在同样 Agent 放置路径下扫描 `max / 50% / 20% / 10% / 5%` background `cpu.max` profile，输出 `sweep_summary.csv` 和推荐 profile；当前跨机保守结论是 `quota_10` 更适合作为默认演示 profile，121 可进一步尝试 `quota_05`，122 在 `0.85` RPS 保留阈值下没有 profile 完全达标
- Nginx quota Sweep Benchmark 使用 `nginx + wrk + background CPU hog` 在同样 Agent 放置路径下扫描相同 profile；121/122 均推荐 `quota_05`，background ratio 均为 `0.0125`，可作为 Nginx 场景的激进候选 profile，但不覆盖 Redis 场景的保守默认 profile
- Mixed Redis+Nginx quota Sweep Benchmark 在同一窗口并发运行 Redis GET/SET 与 Nginx wrk，扫描相同 background `cpu.max` profile；它使用 Redis GET/SET ratio、Nginx RPS ratio 和三者最低保留率作为混合业务边界，121 推荐 `quota_20`，122 推荐 `quota_50`，说明混合场景不能直接套用单 workload 最优 profile
- Mixed Redis+Nginx Multi-Resource Benchmark 在同一混合业务上比较 CPU/cpuset 与 `cpu.max + cpuset.cpus + memory.low/high` 组合 profile；它验证 Agent 对 latency/background cgroup 写入 `cpuset.cpus`、latency `memory.low=67108864`、background `memory.high=134217728`，并验证 applied/restored 审计事件和业务最低保留率
- Policy Engine 联动测试验证 `security_policy` 的 `burst_execve` anomaly 可触发 `policy_engine` 对显式 background cgroup 写入 `cpu.max=10000 100000` 与 `memory.high=1048576`；事件包含 `cross_skill_response result=applied/restored`，ActionJournal 记录旧值和新值，Agent 退出后恢复 `cpu.max=max 100000` 与 `memory.high=max`

当前 121 结果摘要：

```text
result=pass
cpu_max_pressure=10000 100000
memory_high_pressure=1048576
memory_high_events_before=0
memory_high_events_after=590
old_cpu_max=max 100000
old_memory_high=max
```

当前 122 结果摘要：

```text
result=pass
cpu_max_pressure=10000 100000
memory_high_pressure=1048576
memory_high_events_before=0
memory_high_events_after=993
old_cpu_max=max 100000
old_memory_high=max
```

当前 121 IO 结果摘要：

```text
result=pass
io_device=253:0
io_max_pressure=253:0 rbps=max wbps=1048576
io_weight_pressure=default 50
baseline_time_s=0.026
limited_time_s=6.684
baseline_wbytes_after=4194304
limited_wbytes_after=8388608
old_io_weight=default 100
```

当前 122 IO 结果摘要：

```text
result=pass
io_device=253:0
io_max_pressure=253:0 rbps=max wbps=1048576
io_weight_pressure=default 50
baseline_time_s=0.025
limited_time_s=13.784
baseline_wbytes_after=4194304
limited_wbytes_after=8388608
old_io_weight=default 100
```

当前 121 runtime target 结果摘要：

```text
result=pass
target_types=container_id,container,k8s_pod
container_id_cpu_max_pressure=10000 100000
container_name_cpu_max_pressure=10000 100000
k8s_pod_cpu_max_pressure=10000 100000
container_id_memory_high_pressure=1048576
container_name_memory_high_pressure=1048576
k8s_pod_memory_high_pressure=1048576
```

当前 122 runtime target 结果摘要：

```text
result=pass
target_types=container_id,container,k8s_pod
container_id_cpu_max_pressure=10000 100000
container_name_cpu_max_pressure=10000 100000
k8s_pod_cpu_max_pressure=10000 100000
container_id_memory_high_pressure=1048576
container_name_memory_high_pressure=1048576
k8s_pod_memory_high_pressure=1048576
```

当前 121 runtime readiness 诊断摘要（2026-06-30 k3s）：

```text
result=ready
reason=runtime-ready
container_runtime_ready=1
kubernetes_ready=1
podman_command=/usr/bin/podman
crictl_command=/usr/local/bin/crictl
kubectl_command=/usr/bin/kubectl
kubectl_get_ns_rc=0
next_action=none
```

当前 122 runtime readiness 诊断摘要（2026-06-30 k3s）：

```text
result=ready
reason=runtime-ready
container_runtime_ready=1
kubernetes_ready=1
podman_command=/usr/bin/podman
crictl_command=/usr/local/bin/crictl
kubectl_command=/usr/bin/kubectl
kubectl_get_ns_rc=0
next_action=none
```
## 真实 runtime / Pod target 演示入口

当前 121 real runtime target 摘要：

```text
result=pass
reason=real-runtime-target-applied-and-restored
kernel=6.6.0-132.0.0.111.oe2403sp3.x86_64
runtime_kind=podman
runtime_image=localhost/eulerpilot-busybox:latest
target_ref=real_container
target_cgroup=/sys/fs/cgroup/machine.slice/libpod-*.scope/container
cpu_max_pressure=10000 100000
memory_high_pressure=1048576
old_cpu_max=max 100000
old_memory_high=max
```

当前 122 real runtime target 摘要：

```text
result=pass
reason=real-runtime-target-applied-and-restored
kernel=6.6.0-olk66-scx
runtime_kind=podman
runtime_image=localhost/eulerpilot-busybox:latest
target_ref=real_container
target_cgroup=/sys/fs/cgroup/machine.slice/libpod-*.scope/container
cpu_max_pressure=10000 100000
memory_high_pressure=1048576
old_cpu_max=max 100000
old_memory_high=max
```

当前 121 real Pod target 摘要：

```text
result=pass
reason=real-pod-target-applied-and-restored
kernel=6.6.0-132.0.0.111.oe2403sp3.x86_64
pod_namespace=eulerpilot-lab
pod_name=eulerpilot-rc-pod
target_ref=lab_pod
target_cgroup=/sys/fs/cgroup/kubepods/besteffort/podec49c0af-8ae7-462b-8a6d-9fef2b0b62b3
cpu_max_pressure=10000 100000
memory_high_pressure=1048576
```

当前 122 real Pod target 摘要：

```text
result=pass
reason=real-pod-target-applied-and-restored
kernel=6.6.0-olk66-scx
pod_namespace=eulerpilot-lab
pod_name=eulerpilot-rc-pod
target_ref=lab_pod
target_cgroup=/sys/fs/cgroup/kubepods/besteffort/pod03a42fb7-0c29-4c32-9dde-bd5367b9cfcc
cpu_max_pressure=10000 100000
memory_high_pressure=1048576
```

复现条件：
- real runtime target：当前 Podman 路径已在 121/122 pass；Docker 18.09 daemon 因 `Devices cgroup isn't mounted` 暂不作为主验证 runtime，iSulad/isula 可在后续 openEuler 原生 runtime 环境中追加验证。
- real Pod target：121/122 已通过 k3s 准备 `eulerpilot-lab` namespace 和持续运行 demo Pod；若新环境允许脚本创建 demo Pod，显式设置 `EULERPILOT_ALLOW_K8S_CREATE=1`。
当前 121 CPU quota 结果摘要：

```text
result=pass
cpu_max_pressure=10000 100000
baseline_usage_rate_usec_per_s=1001520.00
limited_usage_rate_usec_per_s=100849.00
usage_rate_ratio=0.1007
limited_nr_throttled_delta=61
limited_throttled_usec_delta=5557964
```

当前 122 CPU quota 结果摘要：

```text
result=pass
cpu_max_pressure=10000 100000
baseline_usage_rate_usec_per_s=1001973.50
limited_usage_rate_usec_per_s=100857.00
usage_rate_ratio=0.1007
limited_nr_throttled_delta=61
limited_throttled_usec_delta=5557095
```

当前 121 Redis quota Compare Benchmark 摘要：

```text
result=pass
benchmark=redis_background_cpu_quota_compare
no_quota_get_rps=40160.64
quota_get_rps=37735.85
quota_vs_no_quota_get_rps_ratio=0.9396
no_quota_background_usage_rate_usec_per_s=4037467.65
quota_background_usage_rate_usec_per_s=99623.90
quota_vs_no_quota_background_usage_rate_ratio=0.0247
quota_nr_throttled_delta=17
quota_throttled_usec_delta=6594710
```

当前 122 Redis quota Compare Benchmark 摘要：

```text
result=pass
benchmark=redis_background_cpu_quota_compare
no_quota_get_rps=40650.41
quota_get_rps=36719.71
quota_vs_no_quota_get_rps_ratio=0.9033
no_quota_background_usage_rate_usec_per_s=4038245.65
quota_background_usage_rate_usec_per_s=100796.10
quota_vs_no_quota_background_usage_rate_ratio=0.0250
quota_nr_throttled_delta=17
quota_throttled_usec_delta=6621902
```

当前 121 Redis quota Sweep Benchmark 摘要：

```text
result=pass
benchmark=redis_background_cpu_quota_sweep
quota_10_background_ratio_vs_no_quota=0.0247
quota_10_nr_throttled_delta=15
recommended_profile=quota_05
recommended_cpu_max=5000 100000
recommended_get_ratio_vs_no_quota=0.9558
recommended_set_ratio_vs_no_quota=0.9448
recommended_background_ratio_vs_no_quota=0.0121
recommendation_reason=rps_retention_ge_0.85_and_min_background_ratio
```

当前 122 Redis quota Sweep Benchmark 摘要：

```text
result=pass
benchmark=redis_background_cpu_quota_sweep
quota_10_background_ratio_vs_no_quota=0.0246
quota_10_nr_throttled_delta=16
recommended_profile=quota_10
recommended_cpu_max=10000 100000
recommended_get_ratio_vs_no_quota=0.8423
recommended_set_ratio_vs_no_quota=0.9761
recommended_background_ratio_vs_no_quota=0.0246
recommendation_reason=no_profile_met_rps_retention_threshold
```

跨机解释：121 在 `0.85` RPS 保留阈值下可选择 `quota_05`；122 没有 profile 同时满足 GET/SET 的 `0.85` 保留阈值，最佳折中为 `quota_10`。因此当前默认演示 profile 保守使用 `cpu.max=10000 100000`，`quota_05` 只作为 121 单机进一步调参候选。

当前 121 Nginx quota Sweep Benchmark 摘要：

```text
result=pass
benchmark=nginx_background_cpu_quota_sweep
quota_10_background_ratio_vs_no_quota=0.0249
quota_10_nr_throttled_delta=100
recommended_profile=quota_05
recommended_cpu_max=5000 100000
recommended_rps_ratio_vs_no_quota=1.0012
recommended_background_ratio_vs_no_quota=0.0125
recommended_p99_latency=1.89ms
recommendation_reason=rps_retention_ge_0.85_and_min_background_ratio
```

当前 122 Nginx quota Sweep Benchmark 摘要：

```text
result=pass
benchmark=nginx_background_cpu_quota_sweep
quota_10_background_ratio_vs_no_quota=0.0249
quota_10_nr_throttled_delta=100
recommended_profile=quota_05
recommended_cpu_max=5000 100000
recommended_rps_ratio_vs_no_quota=1.1841
recommended_background_ratio_vs_no_quota=0.0125
recommended_p99_latency=1.88ms
recommendation_reason=rps_retention_ge_0.85_and_min_background_ratio
```

Nginx 跨机解释：121/122 均满足 `0.85` RPS 保留阈值，并推荐 `quota_05`；因此 Nginx 场景可把 `cpu.max=5000 100000` 作为激进演示候选。该结论只适用于当前 Nginx + wrk 场景，不应直接覆盖 Redis 的跨机保守默认 profile。

当前 121 Mixed Redis+Nginx quota Sweep Benchmark 摘要：

```text
result=pass
benchmark=mixed_redis_nginx_background_cpu_quota_sweep
rps_retention_min=0.70
quota_10_background_ratio_vs_no_quota=0.0251
quota_10_business_min_ratio_vs_no_quota=0.6576
recommended_profile=quota_20
recommended_cpu_max=20000 100000
recommended_business_min_ratio_vs_no_quota=0.7073
recommended_redis_get_ratio_vs_no_quota=0.7850
recommended_redis_set_ratio_vs_no_quota=0.7073
recommended_nginx_rps_ratio_vs_no_quota=1.6385
recommended_background_ratio_vs_no_quota=0.0501
recommendation_reason=all_business_retention_ge_0.70_and_min_background_ratio
```

当前 122 Mixed Redis+Nginx quota Sweep Benchmark 摘要：

```text
result=pass
benchmark=mixed_redis_nginx_background_cpu_quota_sweep
rps_retention_min=0.70
quota_10_background_ratio_vs_no_quota=0.0251
quota_10_business_min_ratio_vs_no_quota=0.6258
recommended_profile=quota_50
recommended_cpu_max=50000 100000
recommended_business_min_ratio_vs_no_quota=0.7681
recommended_redis_get_ratio_vs_no_quota=0.7681
recommended_redis_set_ratio_vs_no_quota=0.8832
recommended_nginx_rps_ratio_vs_no_quota=1.0822
recommended_background_ratio_vs_no_quota=0.1255
recommendation_reason=all_business_retention_ge_0.70_and_min_background_ratio
```

Mixed 跨机解释：两台机器都证明 background CPU 会随 `cpu.max` profile 单调下降，`quota_10` 可把 background ratio 压到约 `0.025`，但 Redis GET/SET 与 Nginx 同时运行时，前台最低业务保留率在 `quota_10/quota_05` 下低于 `0.70`。因此混合业务演示应使用按业务最低保留率筛选出的 profile：121 为 `quota_20`，122 为 `quota_50`；如果要给跨机统一保守值，应优先选择 `quota_50` 或在真实现场环境重新跑 sweep 后再冻结。

当前 121 Mixed Redis+Nginx Multi-Resource Benchmark 摘要：

```text
result=pass
benchmark=mixed_redis_nginx_multi_resource_profile
business_retention_min=0.70
cpu_cpuset_quota50_background_ratio_vs_baseline=0.1253
multi_quota50_business_min_ratio_vs_baseline=0.7302
multi_quota50_background_ratio_vs_baseline=0.1257
multi_quota50_latency_memory_low=67108864
multi_quota50_background_memory_high=134217728
multi_quota50_latency_cpuset_cpus=0-1
multi_quota50_background_cpuset_cpus=4-7
recommended_profile=multi_quota50
recommended_cpu_max=50000 100000
```

当前 122 Mixed Redis+Nginx Multi-Resource Benchmark 摘要：

```text
result=pass
benchmark=mixed_redis_nginx_multi_resource_profile
business_retention_min=0.70
cpu_cpuset_quota50_background_ratio_vs_baseline=0.1256
multi_quota50_business_min_ratio_vs_baseline=0.7939
multi_quota50_background_ratio_vs_baseline=0.1257
multi_quota50_latency_memory_low=67108864
multi_quota50_background_memory_high=134217728
multi_quota50_latency_cpuset_cpus=0-1
multi_quota50_background_cpuset_cpus=4-7
recommended_profile=multi_quota50
recommended_cpu_max=50000 100000
```

Multi-Resource 跨机解释：两台机器都验证了 `cpuset.cpus`、`memory.low`、`memory.high` 的 applied/restored 审计事件。当前结果证明 EulerPilot 已具备 CPU quota、CPU placement 与 memory protection 的组合 profile 下发能力；业务侧仍按边界指标解释，不写成通用性能提升结论。

## 后续 TODO

- Kubernetes lab Pod 环境已完成现场演示：真实 Podman container target、真实 k3s Pod cgroup target 和真实 Pod host veth QoS 已完成双机 pass；后续可追加 iSulad/isula、Pod XDP 与真实 Pod 跨 Skill 联动。
- 将当前 Security anomaly -> Resource Control 降级链路扩展到 Network QoS 与 Resource Control 同步限流，并补更多 anomaly 触发源。
