#include "target_resolver.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <net/if.h>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace eulerpilot {

namespace {

bool path_exists(const std::string &path) {
    return access(path.c_str(), F_OK) == 0;
}

bool executable_exists(const std::string &path) {
    return !path.empty() && access(path.c_str(), X_OK) == 0;
}

bool contains_path_separator(const std::string &value) {
    return value.find('/') != std::string::npos;
}

std::vector<std::string> split_colon_list(const std::string &value) {
    std::vector<std::string> items;
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t end = value.find(':', start);
        const std::string item = value.substr(start, end == std::string::npos
                                                       ? std::string::npos
                                                       : end - start);
        if (!item.empty()) {
            items.push_back(item);
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return items;
}

bool command_exists(const std::string &command) {
    if (command.empty()) {
        return false;
    }
    if (contains_path_separator(command)) {
        return executable_exists(command);
    }

    const char *path_env = std::getenv("PATH");
    if (!path_env) {
        return false;
    }
    for (const std::string &dir : split_colon_list(path_env)) {
        if (executable_exists(dir + "/" + command)) {
            return true;
        }
    }
    return false;
}

bool valid_ifname(const std::string &ifname) {
    if (ifname.empty() || ifname.size() >= IF_NAMESIZE) {
        return false;
    }
    for (const char ch : ifname) {
        const auto uch = static_cast<unsigned char>(ch);
        if (!(std::isalnum(uch) || ch == '_' || ch == '-' ||
              ch == '.' || ch == ':')) {
            return false;
        }
    }
    return true;
}

bool socket_exists(const std::string &path) {
    struct stat st {};
    return stat(path.c_str(), &st) == 0 && S_ISSOCK(st.st_mode);
}

std::vector<std::string> runtime_socket_candidates(const TargetResolverOptions &options) {
    const char *override_list = std::getenv("EULERPILOT_TARGET_RUNTIME_SOCKETS");
    if (override_list && *override_list) {
        return split_colon_list(override_list);
    }
    return options.runtime_socket_paths;
}

bool has_runtime_socket(const TargetResolverOptions &options) {
    for (const std::string &path : runtime_socket_candidates(options)) {
        if (socket_exists(path)) {
            return true;
        }
    }
    return false;
}

std::string read_first_cgroup_path(int pid) {
    std::ifstream file("/proc/" + std::to_string(pid) + "/cgroup");
    std::string line;
    while (std::getline(file, line)) {
        auto pos = line.rfind(':');
        if (pos != std::string::npos && pos + 1 < line.size()) {
            return line.substr(pos + 1);
        }
    }
    return {};
}

std::uint64_t inode_as_stable_id(const std::string &path) {
    struct stat st {};
    if (stat(path.c_str(), &st) != 0) {
        return 0;
    }
    return static_cast<std::uint64_t>(st.st_ino);
}

} // namespace

TargetIdentity resolve_pid_target(int pid) {
    TargetIdentity target;
    target.name = "pid:" + std::to_string(pid);
    target.type = "pid";
    target.pid = pid;

    if (pid <= 0) {
        target.reason = "invalid-pid";
        return target;
    }

    std::string relative = read_first_cgroup_path(pid);
    if (relative.empty()) {
        target.reason = "missing-proc-cgroup";
        return target;
    }

    target.cgroup_path = "/sys/fs/cgroup" + relative;
    target.cgroup_id = inode_as_stable_id(target.cgroup_path);
    target.resolved = target.cgroup_id != 0;
    target.reason = target.resolved ? "ok" : "cgroup-path-not-found";
    return target;
}

TargetIdentity resolve_cgroup_target(const std::string &name, const std::string &path) {
    TargetIdentity target;
    target.name = name;
    target.type = "cgroup";
    target.cgroup_path = path;
    target.cgroup_id = inode_as_stable_id(path);
    target.resolved = target.cgroup_id != 0;
    target.reason = target.resolved ? "ok" : "cgroup-path-not-found";
    return target;
}

TargetIdentity resolve_netdev_target(const std::string &name, const std::string &ifname) {
    TargetIdentity target;
    target.name = name;
    target.type = "netdev";
    target.ifname = ifname;

    if (!valid_ifname(ifname)) {
        target.reason = "invalid-ifname";
        return target;
    }

    target.ifindex = static_cast<int>(if_nametoindex(ifname.c_str()));
    target.resolved = target.ifindex > 0 && path_exists("/sys/class/net/" + ifname);
    target.reason = target.resolved ? "ok" : "netdev-not-found";
    return target;
}

TargetIdentity resolve_k8s_pod_target(const K8sPodTargetSpec &spec,
                                      const TargetResolverOptions &options) {
    TargetIdentity target;
    target.name = spec.name.empty()
                      ? "pod:" + spec.pod_namespace + "/" + spec.pod_name
                      : spec.name;
    target.type = "k8s_pod";
    target.pod_namespace = spec.pod_namespace;
    target.pod_name = spec.pod_name;

    if (spec.pod_namespace.empty()) {
        target.reason = "missing-pod-namespace";
        return target;
    }
    if (spec.pod_name.empty()) {
        target.reason = "missing-pod-name";
        return target;
    }
    if (!options.allow_non_lab_pods &&
        spec.pod_namespace != options.lab_namespace) {
        target.reason = "unsupported-namespace";
        return target;
    }
    if (!command_exists(options.kubectl_path)) {
        target.reason = "missing-kubectl";
        return target;
    }
    if (!has_runtime_socket(options)) {
        target.reason = "missing-runtime";
        return target;
    }

    target.reason = "unsupported-pod-veth-resolution";
    return target;
}

TargetIdentity resolve_k8s_pod_target(const std::string &name,
                                      const std::string &pod_namespace,
                                      const std::string &pod_name) {
    return resolve_k8s_pod_target(K8sPodTargetSpec{name, pod_namespace, pod_name});
}

} // namespace eulerpilot
