#!/usr/bin/env bash
set -euo pipefail

QOS_IFACE="${1:-ep-veth-qos0}"
QOS_PEER="${2:-ep-veth-qos1}"
QOS_NETNS="${3:-ep-qos-ns}"

tc qdisc del dev "$QOS_IFACE" root 2>/dev/null || true
tc qdisc del dev "$QOS_IFACE" clsact 2>/dev/null || true
ip link del "$QOS_IFACE" 2>/dev/null || true
ip netns exec "$QOS_NETNS" ip link del "$QOS_PEER" 2>/dev/null || true
ip netns del "$QOS_NETNS" 2>/dev/null || true
