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
    const auto target = eulerpilot::resolve_k8s_pod_target(spec, options);
    std::cout << "resolved=" << (target.resolved ? "1" : "0") << "\n";
    std::cout << "reason=" << target.reason << "\n";
    std::cout << "ifname=" << target.ifname << "\n";
    std::cout << "ifindex=" << target.ifindex << "\n";
    std::cout << "pid=" << target.pid << "\n";
    std::cout << "pod_uid=" << target.pod_uid << "\n";
    std::cout << "container_id=" << target.container_id << "\n";
    std::cout << "netns_path=" << target.netns_path << "\n";
    std::cout << "cgroup_path=" << target.cgroup_path << "\n";
    return target.resolved ? 0 : 1;
}
