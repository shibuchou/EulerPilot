#!/usr/bin/env bash
set -euo pipefail

XDP_IFACE="${1:-ep-veth-xdp0}"
XDP_PEER="${2:-ep-veth-xdp1}"
XDP_NETNS="${3:-ep-xdp-ns}"

ip link set dev "$XDP_IFACE" xdpgeneric off 2>/dev/null || true
ip link set dev "$XDP_IFACE" xdp off 2>/dev/null || true
ip link del "$XDP_IFACE" 2>/dev/null || true
ip netns exec "$XDP_NETNS" ip link del "$XDP_PEER" 2>/dev/null || true
ip netns del "$XDP_NETNS" 2>/dev/null || true
