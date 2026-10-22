#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "ep/domain.hpp"
#include "ep/features.hpp"
#include "ep/journal.hpp"
#include "ep/ownership.hpp"
#include "ep/router.hpp"
#include "ep/skill.hpp"
#include "ep/slo.hpp"

namespace ep {

struct ExecutorOptions {
  bool test_mode = false;
  // Test-only WAL fault injection: fail the Nth journal append (0-based).
  int fail_journal_after = -1;
};

// Single-process transaction engine. Every mutating request goes through
// lease/fence -> generation -> journal(PREPARED) -> apply -> verify -> commit.
class Executor {
 public:
  Executor(std::filesystem::path state_dir, std::unique_ptr<Skill> skill,
           ExecutorOptions options);
  ~Executor();

  bool init(std::string* error);
  Json handle(const Json& request);

  bool degraded() const { return degraded_; }
  std::uint64_t generation() const { return ownership_.generation(); }
  std::size_t journal_size() const { return journal_.size(); }
  const std::string& instance_id() const { return ownership_.instance_id(); }
  std::string skill_name() const { return skill_->name(); }

 private:
  struct Envelope {    std::string request_id;
    std::string transaction_id;
    std::string idempotency_key;
    std::string operation;
    std::string policy_sha256;
    std::string lease_owner;
    std::uint64_t fence_epoch = 0;
    std::uint64_t expected_generation = 0;
    std::int64_t issued_ms = 0;
    std::int64_t deadline_ms = 0;
    Json rollback_of = Json::object();
  };

  Json handle_status(const Json& request);
  Json handle_features(const Json& request);
  Json handle_route(const Json& request);
  Json handle_slo(const Json& request);
  Json handle_lease(const Json& request);
  Json handle_validate(const Json& request);
  Json handle_apply(const Json& request);
  Json handle_rollback(const Json& request);
  Json handle_reconcile(const Json& request);

  bool parse_envelope(const Json& value, bool require_rollback, Envelope* out,
                      std::string* error) const;
  std::string request_digest(const std::string& operation, const Json& payload) const;
  Json rejection(const std::string& request_id, ErrorCode code, const std::string& message,
                 const std::string& detail = "") const;
  Json compensate(const Envelope& envelope, const std::string& action_id,
                  const Snapshot& snapshot, const Applied& applied,
                  const std::string& request_hash, const std::string& cause,
                  ErrorCode code);
  bool reconcile_all(std::vector<std::string>* unresolved, std::string* error);

  struct ActiveAction {
    std::string txn;
    std::string action_id;
    std::string owner;
    std::uint64_t fence = 0;
    Json applied;
    Json snapshot;
    std::string snapshot_id;
    std::string policy_sha256;
    std::int64_t applied_at_ms = 0;
    std::int64_t ttl_ms = 0;
  };

  void monitor_loop();
  void auto_fallback_locked(const std::string& reason);

  std::mutex mu_;
  std::filesystem::path state_dir_;
  std::unique_ptr<Skill> skill_;
  ExecutorOptions options_;
  Ownership ownership_;
  Journal journal_;
  bool degraded_ = false;
  std::atomic<bool> running_{false};
  std::thread monitor_;
  std::optional<ActiveAction> active_;
  std::string last_fallback_;
  std::int64_t next_fallback_attempt_ms_ = 0;
  FeatureWindow features_;
  std::int64_t last_feature_ms_ = 0;
  std::int64_t last_refresh_ms_ = 0;
  RuleRouter router_;
};

}  // namespace ep
