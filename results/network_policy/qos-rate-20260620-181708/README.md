# NetworkQos rate benchmark

Target rate: `2.00 Mbit/s`

- Baseline throughput: `1607.461 Mbit/s`
- Enforced throughput: `1.976 Mbit/s`
- Error vs target: `-1.22%`
- Baseline/enforce reduction ratio: `813.65x`
- Status: `PASS`

Reproduce:

```bash
make agent network-qos-tc
bash tests/benchmark/test_network_qos_rate.sh
```
