#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ep/policy.hpp"
#include "ep/util.hpp"

namespace ep {

// Resolved target identity. cgroup: kernfs inode/device plus the kernel cgroup
// id. pid: pidfd + starttime; the pidfd is held for the lifetime of the
// bindings so the process identity cannot be recycled under the transaction.
struct TargetBinding {
  std::string kind;
  std::string path;
  std::string role;
  std::string role_source;
  std::string boot_id;
  std::uint64_t inode = 0;
  std::uint64_t device = 0;
  std::uint64_t cgroup_id = 0;  // 0 = not available on this kernel/mount
  std::int64_t pid = 0;
  std::uint64_t starttime = 0;
  std::shared_ptr<int> pidfd;  // pid targets; shared by copies, closes on last

  Json to_json() const;
};

// Opens every target as a cgroup v2 directory (O_PATH|O_NOFOLLOW) and records
// path + inode + device + boot_id. Fails closed on a missing/non-v2/symlink path.
bool resolve_targets(const std::vector<PolicyTarget>& targets,
                     std::vector<TargetBinding>* bindings, std::string* error);

// Re-checks boot_id and every path/inode/device before or after a write.
bool revalidate_targets(const std::vector<TargetBinding>& bindings, std::string* error);

}  // namespace ep
