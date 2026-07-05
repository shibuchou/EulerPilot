# EulerPilot SP4 initial validation

- time: 2026-07-05T16:01:56+08:00
- host: cernet2.net
- os: openEuler release 24.03 (LTS-SP4)
- kernel: 6.6.0-159.4.3.154.oe2403sp4.x86_64
- repo_head: d5c3fb3
- cgroup: cgroup2
- controllers: cpuset cpu io memory dmem xcu hugetlb pids rdma misc
- psi_cpu: some avg10=0.00 avg60=0.02 avg300=0.94 total=16520171
- sched_ext: missing
- final_quality_gate: pass
- web_console: pass

## Notes
- SP4 kernel has BTF/BPF/LSM/cgroup v2/PSI available after adding boot args.
- CONFIG_SCHED_CLASS_EXT is not set, so sched_ext/scx remains unavailable on this SP4 kernel.
