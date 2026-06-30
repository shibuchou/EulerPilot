#include "target_resolver.hpp"
#include <iostream>

int main(int argc, char **argv) {
    if (argc != 6) {
        std::cerr << "usage: resolve <namespace> <pod> <container-name> <kubectl> <crictl>\n";
        return 2;
    }
    eulerpilot::K8sPodTargetSpec spec;
    spec.name = "lab_pod";
    spec.pod_namespace = argv[1];
    spec.pod_name = argv[2];
    spec.container_name = argv[3];
    eulerpilot::TargetResolverOptions options;
    options.kubectl_path = argv[4];
    options.crictl_path = argv[5];
    options.require_runtime_socket = false;
    options.lab_namespace = "eulerpilot-lab";

    const auto cgroup = eulerpilot::resolve_k8s_pod_cgroup_target(spec, options);
    const auto netdev = eulerpilot::resolve_k8s_pod_target(spec, options);
    std::cout << "cgroup_resolved=" << (cgroup.resolved ? "1" : "0") << "\n";
    std::cout << "cgroup_reason=" << cgroup.reason << "\n";
    std::cout << "cgroup_path=" << cgroup.cgroup_path << "\n";
    std::cout << "cgroup_id=" << cgroup.cgroup_id << "\n";
    std::cout << "pod_uid=" << cgroup.pod_uid << "\n";
    std::cout << "netdev_resolved=" << (netdev.resolved ? "1" : "0") << "\n";
    std::cout << "netdev_reason=" << netdev.reason << "\n";
    std::cout << "ifname=" << netdev.ifname << "\n";
    std::cout << "ifindex=" << netdev.ifindex << "\n";
    std::cout << "pid=" << netdev.pid << "\n";
    std::cout << "container_id=" << netdev.container_id << "\n";
    std::cout << "netns_path=" << netdev.netns_path << "\n";
    return cgroup.resolved && netdev.resolved ? 0 : 1;
}
