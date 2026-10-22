#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "ep/util.hpp"

namespace ep {

struct LeaseState {
  std::string owner;
  std::uint64_t fence = 0;
  std::int64_t expires_ms = 0;

  bool active(std::int64_t now_ms) const {
    return !owner.empty() && fence > 0 && now_ms < expires_ms;
  }
  Json to_json() const;
};

// Host-wide ownership: one process holds an exclusive flock for its lifetime,
// owns the generation counter and issues fencing leases to agents.
class Ownership {
 public:
  explicit Ownership(std::filesystem::path dir);
  ~Ownership();
  Ownership(const Ownership&) = delete;
  Ownership& operator=(const Ownership&) = delete;

  bool open(std::string* error);

  const std::string& instance_id() const { return instance_id_; }
  std::uint64_t generation() const { return generation_; }
  bool commit_generation(std::uint64_t generation, std::string* error);

  const LeaseState& lease() const { return lease_; }
  bool acquire_lease(const std::string& owner, std::uint64_t ttl_ms, LeaseState* out,
                     std::string* error);
  bool validate_lease(const std::string& owner, std::uint64_t fence,
                      std::string* error) const;

 private:
  bool persist(std::string* error);

  std::filesystem::path dir_;
  std::filesystem::path lock_path_;
  std::filesystem::path state_path_;
  int lock_fd_ = -1;
  std::string instance_id_;
  std::uint64_t generation_ = 0;
  LeaseState lease_;
};

}  // namespace ep
