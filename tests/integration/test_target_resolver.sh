#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CXX="${CXX:-g++}"
TMP_DIR="$(mktemp -d)"
NETNS_NAME=""
HOST_IFACE=""
POD_PID=""
NETNS_JOB=""

cleanup() {
    if [[ -n "$NETNS_JOB" ]]; then
        kill "$NETNS_JOB" >/dev/null 2>&1 || true
        wait "$NETNS_JOB" >/dev/null 2>&1 || true
    fi
    if [[ -n "$POD_PID" ]]; then
        kill "$POD_PID" >/dev/null 2>&1 || true
        wait "$POD_PID" >/dev/null 2>&1 || true
    fi
    if [[ -n "$NETNS_NAME" ]]; then
        ip netns del "$NETNS_NAME" >/dev/null 2>&1 || true
    fi
    if [[ -n "$HOST_IFACE" ]]; then
        ip link del "$HOST_IFACE" >/dev/null 2>&1 || true
    fi
    rm -rf "$TMP_DIR"
}
trap cleanup EXIT

FAKE_KUBECTL="$TMP_DIR/kubectl"
FAKE_CRICTL="$TMP_DIR/crictl"
SELFTEST_CPP="$TMP_DIR/target_resolver_selftest.cpp"
SELFTEST_BIN="$TMP_DIR/target_resolver_selftest"

cat >"$FAKE_KUBECTL" <<'SH'
#!/bin/sh
case "$*" in
    *metadata.uid*)
        printf '%s\n' '123e4567-e89b-12d3-a456-426614174000'
        ;;
    *containerStatuses*)
        printf '%s\n' 'containerd://abcdef1234567890'
        ;;
    *)
        exit 1
        ;;
esac
SH
chmod +x "$FAKE_KUBECTL"

cat >"$FAKE_CRICTL" <<'SH'
#!/bin/sh
if [ "$1" = "inspect" ]; then
    printf '{"info":{"pid":%s}}\n' "${EULERPILOT_TEST_POD_PID:-0}"
    exit 0
fi
exit 1
SH
chmod +x "$FAKE_CRICTL"

POD_PEER_IFACE="eppod$$"
HOST_IFACE="ephost$$"
if [[ "$(id -u)" -eq 0 ]] && command -v ip >/dev/null 2>&1; then
    NETNS_NAME="ep-target-$$"
    ip netns add "$NETNS_NAME"
    ip link add "$HOST_IFACE" type veth peer name "$POD_PEER_IFACE"
    ip link set "$POD_PEER_IFACE" netns "$NETNS_NAME"
    ip link set "$HOST_IFACE" up
    ip -n "$NETNS_NAME" link set lo up
    ip -n "$NETNS_NAME" link set "$POD_PEER_IFACE" name eth0
    ip -n "$NETNS_NAME" link set eth0 up
    ip netns exec "$NETNS_NAME" sleep 120 &
    NETNS_JOB="$!"
    sleep 0.2
    POD_PID="$(ip netns pids "$NETNS_NAME" | head -n 1 || true)"
    if [[ -z "$POD_PID" ]]; then
        POD_PID="$NETNS_JOB"
    fi
else
    HOST_IFACE="skip"
    POD_PID="0"
fi
export EULERPILOT_TEST_POD_PID="$POD_PID"

cat >"$SELFTEST_CPP" <<'CPP'
#include "target_resolver.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const std::string &message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        ++failures;
    }
}

void expect_reason(const eulerpilot::TargetIdentity &target,
                   const std::string &reason,
                   const std::string &message) {
    expect(!target.resolved, message + " should not resolve");
    expect(target.reason == reason,
           message + " reason expected " + reason + ", got " + target.reason);
}

eulerpilot::K8sPodTargetSpec pod_spec(const std::string &pod_namespace,
                                      const std::string &pod_name) {
    eulerpilot::K8sPodTargetSpec spec;
    spec.name = "web";
    spec.pod_namespace = pod_namespace;
    spec.pod_name = pod_name;
    return spec;
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 4) {
        std::cerr << "usage: target_resolver_selftest <fake-kubectl> <fake-crictl> <expected-host-iface>\n";
        return 2;
    }

    const auto loopback = eulerpilot::resolve_netdev_target("loopback", "lo");
    expect(loopback.resolved, "loopback netdev resolves");
    expect(loopback.ifindex > 0, "loopback ifindex is positive");
    expect(loopback.reason == "ok", "loopback reason is ok");

    bool saw_missing_netdev = false;
    const char *missing_candidates[] = {"epmiss0", "epmiss1", "epmiss2", "zzep0"};
    for (const char *candidate : missing_candidates) {
        const auto missing_netdev =
            eulerpilot::resolve_netdev_target("missing", candidate);
        if (!missing_netdev.resolved &&
            missing_netdev.reason == "netdev-not-found") {
            saw_missing_netdev = true;
            break;
        }
    }
    expect(saw_missing_netdev, "missing netdev returns netdev-not-found");

    const auto invalid_netdev =
        eulerpilot::resolve_netdev_target("invalid", "bad/name");
    expect_reason(invalid_netdev, "invalid-ifname", "invalid netdev");

    eulerpilot::TargetResolverOptions options;
    const auto unsupported_namespace = eulerpilot::resolve_k8s_pod_target(
        pod_spec("default", "web-demo"), options);
    expect_reason(unsupported_namespace, "unsupported-namespace",
                  "unsupported namespace");

    const char *saved_path = std::getenv("PATH");
    const std::string original_path = saved_path ? saved_path : "";
    setenv("PATH", "/nonexistent-eulerpilot-path", 1);
    const auto missing_kubectl = eulerpilot::resolve_k8s_pod_target(
        pod_spec("eulerpilot-lab", "web-demo"), options);
    expect_reason(missing_kubectl, "missing-kubectl", "missing kubectl");
    setenv("PATH", original_path.c_str(), 1);

    options.kubectl_path = argv[1];
    options.runtime_socket_paths = {"/nonexistent-eulerpilot-runtime.sock"};
    const auto missing_runtime = eulerpilot::resolve_k8s_pod_target(
        pod_spec("eulerpilot-lab", "web-demo"), options);
    expect_reason(missing_runtime, "missing-runtime", "missing runtime");

    const std::string expected_host_ifname = argv[3];
    if (expected_host_ifname != "skip") {
        options.crictl_path = argv[2];
        options.require_runtime_socket = false;
        const auto pod_veth = eulerpilot::resolve_k8s_pod_target(
            pod_spec("eulerpilot-lab", "web-demo"), options);
        expect(pod_veth.resolved, "pod veth resolves with fake kubectl/crictl, reason " +
                                      pod_veth.reason);
        expect(pod_veth.reason == "ok", "pod veth reason is ok, got " +
                                         pod_veth.reason);
        expect(pod_veth.ifname == expected_host_ifname,
               "pod veth host ifname matches expected, got " +
                   pod_veth.ifname + ", expected " + expected_host_ifname);
        expect(pod_veth.ifindex > 0, "pod veth ifindex is positive");
        expect(pod_veth.pid > 0, "pod veth runtime pid is recorded");
        expect(!pod_veth.netns_path.empty(), "pod veth netns path is recorded");
        expect(pod_veth.container_id == "abcdef1234567890",
               "pod container id is parsed from kubectl runtime URI");
        expect(pod_veth.pod_uid == "123e4567-e89b-12d3-a456-426614174000",
               "pod uid is parsed from kubectl");
    }

    if (failures != 0) {
        return 1;
    }
    std::cout << "PASS: target resolver netdev and k8s_pod runtime/veth paths\n";
    return 0;
}
CPP

"$CXX" -std=c++17 -Wall -Wextra -I"$ROOT_DIR/agent/include" \
    "$ROOT_DIR/agent/src/target_resolver.cpp" \
    "$SELFTEST_CPP" \
    -o "$SELFTEST_BIN"

"$SELFTEST_BIN" "$FAKE_KUBECTL" "$FAKE_CRICTL" "$HOST_IFACE"
