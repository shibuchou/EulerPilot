# NetworkXDP integration result

Run directory: `/root/EulerPilot/results/network_policy/xdp-20260703-122-fields-v1`

This test validates the `network_xdp` sub-skill on an isolated veth pair.

- Baseline: netns peer can ping host-side veth.
- Audit: Agent starts without attaching XDP.
- Enforce: Agent attaches XDP generic mode on `ep-veth-xdp0`, blocks ICMP connectivity, and records TCP:`19092`, UDP:`19093`, plus UDP tuple `10.89.0.2:39094 -> 10.89.0.1:19094` rule hits.
- Rollback: Agent detaches XDP and connectivity recovers.

Reproduce:

```bash
make agent network-xdp-demo
bash tests/integration/test_network_xdp.sh
```
