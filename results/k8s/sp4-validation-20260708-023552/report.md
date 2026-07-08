# SP4 / Kubernetes 旁路验证报告

时间：`2026-07-08 10:35 CST`

## 结论

本次验证通过。SP4 主验证仓库和 Kubernetes master 已完成只读盘点、独立 namespace 最小写入验证、Web Console 白名单动作验证、cleanup 复查和最终质量门禁。

## 环境

- SP4 主验证仓库：`192.168.1.123:/root/EulerPilot`
- SP4 系统：`openEuler 24.03 LTS SP4`
- SP4 内核：`6.6.0-159.4.3.154.oe2403sp4.x86_64-eulerpilot-scx`
- SP4 能力：`cgroup_v2=true`、`psi=true`、`sched_ext=true`、`btf=true`
- Kubernetes context：`kubernetes-admin@kubernetes`
- Kubernetes 版本：`v1.29.15`
- 节点：`k8s-master`、`k8s-worker1`、`k8s-worker2` 均为 `Ready`

## 只读基线

验证前 `app.kubernetes.io/part-of=eulerpilot-validation` 与 `eulerpilot.io/owner=web-console` 两个 label selector 均未发现历史 EulerPilot 残留。

验证前已有的非 Running 项包括：

- `ebpf-service-system/ebpf-clusterip-agent-h8lw2`：`ImagePullBackOff`
- `ebpf-service-test/clusterip-client-cnf74`：`ImagePullBackOff`
- 若干 `Completed` 导入任务

这些是本次验证前已有状态，不属于 EulerPilot 本次验证影响。

## 最小写入验证

本次仅创建：

- Namespace：`eulerpilot-sp4-validation`
- Deployment：`eulerpilot-k8s-smoke`
- Service：`eulerpilot-k8s-smoke`

所有资源均带：

```text
app.kubernetes.io/part-of=eulerpilot-validation
eulerpilot.io/owner=web-console
```

Pod 资源限制：

```text
requests: cpu=10m, memory=16Mi
limits:   cpu=50m, memory=64Mi
```

验证结果：

- Deployment rollout：通过
- Pod 本地健康检查：`ok`
- Service 健康检查：`ok`

原始文件：

- `k8s-master-raw/apply.txt`
- `k8s-master-raw/rollout.txt`
- `k8s-master-raw/pod_local_health.txt`
- `k8s-master-raw/service_health.txt`

## Web Console 验证

SP4 Web Console 运行在 `127.0.0.1:18080`。

API 验证：

- `/api/health`：通过
- `/api/system`：显示 SP4 + sched_ext 可用
- `/api/evidence/summary`：35 条核心证据、必需缺失 0、警告 0

白名单动作验证：

- `check_env`：通过
- `list_skills`：通过
- `status_json`：通过
- `validate_default_config`：通过
- `doctor_skills`：通过
- `demo_offline`：通过
- `demo_cleanup`：通过

原始文件：

- `web_console/evidence_summary_after_manifest.json`
- `web_console/web_console_action_summary_v2.json`
- `web_console/jobs_after.json`

## Cleanup 结果

已删除 `eulerpilot-sp4-validation` namespace。清理后复查：

```text
kubectl get all -A -l app.kubernetes.io/part-of=eulerpilot-validation
kubectl get all -A -l eulerpilot.io/owner=web-console
```

两者均返回 `No resources found`。

原始文件：

- `k8s-master-raw/validation_label_after_cleanup.txt`
- `k8s-master-raw/webconsole_label_after_cleanup.txt`
- `k8s-master-raw/nodes_after_cleanup.txt`
- `k8s-master-raw/kube_system_after_cleanup.txt`
- `k8s-master-raw/varmor_after_cleanup.txt`

## 质量门禁

SP4 上 `scripts/final_quality_gate.sh` 完整通过：

- 22/22 P0
- agent 100-round stress smoke：通过
- doctor 5-round stable：通过

原始文件：

- `final_quality_gate.log`

## 已知说明

K8s 验证脚本业务状态为 `status=0`，但 PowerShell 管道执行时在脚本尾部出现一次 `exit: 0\r: numeric argument required`，这是本机 CRLF 传输导致的退出码解析问题；不影响已经完成的 K8s apply、健康检查和 cleanup 证据。后续若固化脚本，应在发送到远端前统一 LF。
