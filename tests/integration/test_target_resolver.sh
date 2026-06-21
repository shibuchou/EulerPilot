#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CXX="${CXX:-g++}"
TMP_DIR="$(mktemp -d)"

cleanup() {
    rm -rf "$TMP_DIR"
}
trap cleanup EXIT

FAKE_KUBECTL="$TMP_DIR/kubectl"
SELFTEST_CPP="$TMP_DIR/target_resolver_selftest.cpp"
SELFTEST_BIN="$TMP_DIR/target_resolver_selftest"

cat >"$FAKE_KUBECTL" <<'SH'
#!/usr/bin/env sh
exit 0
SH
chmod +x "$FAKE_KUBECTL"

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

} // namespace

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "usage: target_resolver_selftest <fake-kubectl>\n";
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
        eulerpilot::K8sPodTargetSpec{"web", "default", "web-demo"},
        options);
    expect_reason(unsupported_namespace, "unsupported-namespace",
                  "unsupported namespace");

    setenv("PATH", "/nonexistent-eulerpilot-path", 1);
    const auto missing_kubectl = eulerpilot::resolve_k8s_pod_target(
        eulerpilot::K8sPodTargetSpec{"web", "eulerpilot-lab", "web-demo"},
        options);
    expect_reason(missing_kubectl, "missing-kubectl", "missing kubectl");

    options.kubectl_path = argv[1];
    options.runtime_socket_paths = {"/nonexistent-eulerpilot-runtime.sock"};
    const auto missing_runtime = eulerpilot::resolve_k8s_pod_target(
        eulerpilot::K8sPodTargetSpec{"web", "eulerpilot-lab", "web-demo"},
        options);
    expect_reason(missing_runtime, "missing-runtime", "missing runtime");

    if (failures != 0) {
        return 1;
    }
    std::cout << "PASS: target resolver netdev and k8s_pod error paths\n";
    return 0;
}
CPP

"$CXX" -std=c++17 -Wall -Wextra -I"$ROOT_DIR/agent/include" \
    "$ROOT_DIR/agent/src/target_resolver.cpp" \
    "$SELFTEST_CPP" \
    -o "$SELFTEST_BIN"

"$SELFTEST_BIN" "$FAKE_KUBECTL"
