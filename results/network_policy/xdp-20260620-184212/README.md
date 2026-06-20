# NetworkXDP integration result

Run directory: `results/network_policy/xdp-20260620-184212`

This test validates the `network_xdp` sub-skill on an isolated veth pair.

- Baseline: netns peer can ping host-side veth.
- Audit: Agent starts without attaching XDP.
- Enforce: Agent attaches XDP generic mode on `ep-veth-xdp0`, blocks ICMP connectivity, and records TCP:`19092` rule hits.
- Rollback: Agent detaches XDP and connectivity recovers.

Reproduce:

```bash
make agent network-xdp-demo
bash tests/integration/test_network_xdp.sh
```
