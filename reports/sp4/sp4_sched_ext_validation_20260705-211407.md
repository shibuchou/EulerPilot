# SP4 sched_ext Kernel Validation

- date: `2026-07-05`
- host: `openEuler-2403-LTS-SP4`
- project path: `/root/EulerPilot`
- baseline commit: `cd0add4`
- validation kernel: `6.6.0-159.4.3.154.oe2403sp4.x86_64-eulerpilot-scx`

## Kernel Result

The custom SP4 kernel booted successfully with:

```text
CONFIG_SCHED_CLASS_EXT=y
CONFIG_EXT_GROUP_SCHED=y
/sys/kernel/sched_ext present
```

Boot command line kept the EulerPilot required cgroup v2 and PSI options:

```text
systemd.unified_cgroup_hierarchy=1 cgroup_no_v1=all psi=1
```

Runtime probes confirmed:

```text
cgroup2 /sys/fs/cgroup
psi_cpu=ok
psi_memory=ok
psi_io=ok
bpftool: struct_ops program/map types available
```

## EulerPilot Result

SP4 sched_ext kernel passed the project checks:

```text
scripts/check_sp4_env.sh
make agent
./build/eulerpilot-agent --validate-config configs/agent.yaml
./build/eulerpilot-agent --doctor-skills --config configs/agent.yaml
scripts/final_quality_gate.sh
```

`final_quality_gate.sh` result:

```text
21/21 P0 passed
agent 100-round stress smoke passed
doctor 5-round stable passed
```

Evidence:

```text
reports/sp4/final_quality_gate_scx_20260705-205406.log
```

## v3.1 Cross-Skill Validation

The v3.1 Policy Engine chain passed on the SP4 sched_ext kernel:

```text
security_policy burst_connect
  -> policy_engine
  -> resource_control cpu.max/memory.high
  -> network_qos tc/tbf
  -> rollback
```

Single run:

```text
reports/sp4/policy_engine_security_network_resource_scx_20260705-211329.log
results/policy_engine/security-network-resource-20260705-211329
```

Repeat 10:

```text
reports/sp4/policy_engine_security_network_resource_repeat10_scx_20260705-211407.log
results/policy_engine/security-network-resource-20260705-211407
```

Repeat 10 summary:

```text
result=pass
repeat=10
success_cases=10
failure_rollback=pass
```

## Web Console

Web Console restarted successfully on `127.0.0.1:18080` after the SP4 sched_ext reboot.

API checks:

```text
/api/health ok
/api/system sched_ext=true
/api/evidence/summary total=28 required_missing=0 warnings=0
```

The SP3/SP4 status must be shown as path roles:

```text
SP3 official path: cgroup v2 active
sched_ext/scx: enhanced path available
```

## Environment Fix

SP4 exposed a portability issue in `scripts/setup_cgroup_v2.sh`: fixed cpuset values such as `2-3` or `4-7` can be invalid on small virtual machines. The setup script now attempts the previous default cpuset layout first and falls back to the parent cgroup effective CPU/MEM set when needed.
