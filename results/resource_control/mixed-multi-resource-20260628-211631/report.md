# Mixed Redis + Nginx Multi-Resource Profile Benchmark

- result: `pass`
- business retention threshold: `0.70`
- recommended profile: `multi_quota50`
- recommended cpu.max: `50000 100000`

## Profile Results

| Profile | cpu.max | Memory | Redis GET Ratio | Redis SET Ratio | Nginx RPS Ratio | Business Min Ratio | Nginx p99 | Background Ratio | latency memory.low | background memory.high |
|---------|---------|--------|-----------------|-----------------|-----------------|--------------------|-----------|------------------|--------------------|------------------------|
| cpu_cpuset_no_quota | `max` | false | 1.0000 | 1.0000 | 1.0000 | 1.0000 | 2.70ms | 1.0000 | `0` | `max` |
| cpu_cpuset_quota50 | `50000 100000` | false | 0.8619 | 0.7777 | 1.3954 | 0.7777 | 3.72ms | 0.1253 | `0` | `max` |
| multi_quota50 | `50000 100000` | true | 0.7302 | 0.7412 | 1.3998 | 0.7302 | 2.91ms | 0.1257 | `67108864` | `134217728` |
| multi_quota20 | `20000 100000` | true | 0.7077 | 0.6587 | 1.5382 | 0.6587 | 1.49ms | 0.0502 | `67108864` | `134217728` |

## Interpretation

This benchmark compares the existing CPU/cpuset placement path with a multi-resource profile that also writes latency `memory.low` and background `memory.high`. It keeps Redis and Nginx running together and uses Redis GET/SET plus Nginx RPS as foreground boundary signals. The goal is to prove that EulerPilot can apply a combined CPU, cpuset, and memory protection profile with audit and rollback evidence, not to claim universal business throughput improvement.

## Artifacts

- `profile_summary.csv`
- `summary.txt`
- `resource_control_events.jsonl`
- `agent.<profile>.log`
- `<profile>.redis.csv` and `<profile>.redis.summary.csv`
- `<profile>.wrk.txt` and `<profile>.nginx.summary.csv`
