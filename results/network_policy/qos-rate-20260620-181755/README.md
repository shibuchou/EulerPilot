# NetworkQos rate benchmark

Target rate: `2.00 Mbit/s`

- Baseline throughput: `1796.228 Mbit/s`
- Enforced throughput: `1.971 Mbit/s`
- Error vs target: `-1.45%`
- Baseline/enforce reduction ratio: `911.33x`
- Status: `PASS`

Reproduce:

```bash
make agent network-qos-tc
bash tests/benchmark/test_network_qos_rate.sh
```
