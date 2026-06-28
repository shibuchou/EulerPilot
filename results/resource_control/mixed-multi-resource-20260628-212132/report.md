# Mixed Redis + Nginx Multi-Resource Profile Benchmark

- result: `pass`
- business retention threshold: `0.70`
- recommended profile: `multi_quota50`
- recommended cpu.max: `50000 100000`

## Profile Results

| Profile | cpu.max | Memory | Redis GET Ratio | Redis SET Ratio | Nginx RPS Ratio | Business Min Ratio | Nginx p99 | Background Ratio | latency memory.low | background memory.high |
|---------|---------|--------|-----------------|-----------------|-----------------|--------------------|-----------|------------------|--------------------|------------------------|
| cpu_cpuset_no_quota | `max` | false | 1.0000 | 1.0000 | 1.0000 | 1.0000 | 19.10ms | 1.0000 | `0` | `max` |
| cpu_cpuset_quota50 | `50000 100000` | false | 0.7124 | 0.7180 | 1.2278 | 0.7124 | 1.47ms | 0.1256 | `0` | `max` |
| multi_quota50 | `50000 100000` | true | 0.9662 | 0.7939 | 1.1223 | 0.7939 | 1.87ms | 0.1257 | `67108864` | `134217728` |
| multi_quota20 | `20000 100000` | true | 0.9135 | 0.6646 | 1.2542 | 0.6646 | 1.38ms | 0.0504 | `67108864` | `134217728` |

## Interpretation

This benchmark compares the existing CPU/cpuset placement path with a multi-resource profile that also writes latency `memory.low` and background `memory.high`. It keeps Redis and Nginx running together and uses Redis GET/SET plus Nginx RPS as foreground boundary signals. The goal is to prove that EulerPilot can apply a combined CPU, cpuset, and memory protection profile with audit and rollback evidence, not to claim universal business throughput improvement.

## Artifacts

- `profile_summary.csv`
- `summary.txt`
- `resource_control_events.jsonl`
- `agent.<profile>.log`
- `<profile>.redis.csv` and `<profile>.redis.summary.csv`
- `<profile>.wrk.txt` and `<profile>.nginx.summary.csv`
