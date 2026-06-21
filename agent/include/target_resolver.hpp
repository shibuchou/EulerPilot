#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace eulerpilot {

struct TargetIdentity {
    std::string name;
    std::string type;
    int pid = -1;
    std::uint64_t cgroup_id = 0;
    std::string cgroup_path;
    std::string container_id;
    std::string pod_namespace;
    std::string pod_name;
    std::string netns_path;
    std::string ifname;
    int ifindex = 0;
    bool resolved = false;
    std::string reason;
};

struct K8sPodTargetSpec {
    std::string name;
    std::string pod_namespace;
    std::string pod_name;
};

struct TargetResolverOptions {
    bool allow_non_lab_pods = false;
    std::string lab_namespace = "eulerpilot-lab";
    std::string kubectl_path = "kubectl";
    std::vector<std::string> runtime_socket_paths = {
        "/run/containerd/containerd.sock",
        "/run/crio/crio.sock",
        "/var/run/cri-dockerd.sock",
        "/var/run/dockershim.sock",
    };
};

TargetIdentity resolve_pid_target(int pid);
TargetIdentity resolve_cgroup_target(const std::string &name, const std::string &path);
TargetIdentity resolve_netdev_target(const std::string &name, const std::string &ifname);
TargetIdentity resolve_k8s_pod_target(
    const K8sPodTargetSpec &spec,
    const TargetResolverOptions &options = TargetResolverOptions{});
TargetIdentity resolve_k8s_pod_target(const std::string &name,
                                      const std::string &pod_namespace,
                                      const std::string &pod_name);

} // namespace eulerpilot
