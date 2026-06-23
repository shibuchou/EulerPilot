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
    std::string container_name;
    std::string pod_namespace;
    std::string pod_name;
    std::string pod_uid;
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
    std::string pod_uid;
    std::string container_id;
    std::string container_name;
    std::string cgroup_root = "/sys/fs/cgroup";
};

struct ContainerTargetSpec {
    std::string name;
    std::string container_id;
    std::string container_name;
    std::string runtime = "auto";
    std::string cgroup_root = "/sys/fs/cgroup";
    std::string crictl_path = "crictl";
    std::string docker_path = "docker";
    std::string podman_path = "podman";
};

struct TargetResolverOptions {
    bool allow_non_lab_pods = false;
    bool allow_host_network_pods = false;
    bool require_runtime_socket = true;
    std::string lab_namespace = "eulerpilot-lab";
    std::string kubectl_path = "kubectl";
    std::string crictl_path = "crictl";
    std::string docker_path = "docker";
    std::string podman_path = "podman";
    std::string ip_path = "ip";
    std::string nsenter_path = "nsenter";
    std::vector<std::string> runtime_socket_paths = {
        "/run/containerd/containerd.sock",
        "/run/crio/crio.sock",
        "/var/run/cri-dockerd.sock",
        "/var/run/dockershim.sock",
    };
};

TargetIdentity resolve_pid_target(int pid);
TargetIdentity resolve_cgroup_target(const std::string &name, const std::string &path);
TargetIdentity resolve_container_target(const std::string &name,
                                        const std::string &container_id,
                                        const std::string &cgroup_root = "/sys/fs/cgroup");
TargetIdentity resolve_container_target(const ContainerTargetSpec &spec);
TargetIdentity resolve_netdev_target(const std::string &name, const std::string &ifname);
TargetIdentity resolve_k8s_pod_cgroup_target(
    const K8sPodTargetSpec &spec,
    const TargetResolverOptions &options = TargetResolverOptions{});
TargetIdentity resolve_k8s_pod_target(
    const K8sPodTargetSpec &spec,
    const TargetResolverOptions &options = TargetResolverOptions{});
TargetIdentity resolve_k8s_pod_target(const std::string &name,
                                      const std::string &pod_namespace,
                                      const std::string &pod_name);

} // namespace eulerpilot
