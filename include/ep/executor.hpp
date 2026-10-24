#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <map>
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
  // Autopilot: move the policy that is already in place between whitelisted
  // experts. Off unless the caller supplies both a baseline policy and an owner
  // identity, and it only acts while that owner holds the lease.
  bool auto_apply = false;
  std::string auto_owner;
  std::string auto_baseline_path;
  Json auto_baseline_policy = Json::object();
  std::int64_t auto_min_interval_ms = 15000;
  std::int64_t auto_lease_ttl_ms = 30000;
  // Suppression, sized from the measured cost of a switch (621-665ms with no
  // v2 policy in place). Dwell keeps us from leaving a policy we just installed;
  // candidate hold refuses routes that keep flipping; the budget bounds how much
  // unprotected time the autopilot may cause per window.
  std::int64_t auto_dwell_ms = 60000;
  std::int64_t auto_candidate_hold_ms = 15000;
  std::int64_t auto_budget_window_ms = 600000;
  int auto_budget_max_switches = 2;
  // Consecutive violating measurement windows required before a switch is
  // recorded as service evidence against the expert it moved to. One window of
  // p99 latency moves more than a declared budget on noise alone.
  int auto_outcome_windows = 2;
  std::int64_t auto_measured_downtime_ms = 665;

  // Constructors keep every existing ExecutorOptions{true} call site warning-free
  // now that the struct carries more than the two original fields.
  ExecutorOptions() = default;
  explicit ExecutorOptions(bool test_mode_value) : test_mode(test_mode_value) {}
  ExecutorOptions(bool test_mode_value, int fail_journal)
      : test_mode(test_mode_value), fail_journal_after(fail_journal) {}
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

  // Stop the monitor and release whatever effect is still in place, so an exiting
  // service leaves the kernel state verified rather than relying on a fd closing by
  // accident and leaving pinned maps behind for someone else to find.
  void shutdown(const std::string& reason);

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
  Json handle_observe(const Json& request);
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

  // Service telemetry is advisory: it can refuse a change and never cause one.
  // The same input feeds the advisory RPC and the autopilot so both see one set
  // of limits -- the ones the active policy declared.
  // Non-const: it records the telemetry health high-water while answering.
  SloInput service_slo_input(const std::string& expert, std::int64_t now_ms);
  ServiceNoise service_noise(const std::string& expert) const;

  struct ActiveAction {
    std::string txn;
    std::string action_id;
    std::string owner;
    std::uint64_t fence = 0;
    Json applied;
    Json snapshot;
    std::string snapshot_id;
    std::string policy_sha256;
    // Expert of the policy currently in place; the autopilot compares its target
    // against this instead of re-deriving it from the skill.
    std::string policy_expert;
    std::int64_t applied_at_ms = 0;
    std::int64_t ttl_ms = 0;
    std::vector<TargetBinding> bindings;
  };

  void monitor_loop();
  void auto_fallback_locked(const std::string& reason);
  // Caller must hold mu_. Pure decision plus one apply attempt through the same
  // lease/fence/journal path a client uses.
  void run_auto_apply_locked(std::int64_t now_ms);

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
  // Last time the audit/state store could not be written, and what was done about it.
  // Kept after recovery so an unwritable disk stays reportable from policy.status.
  std::string last_store_warning_;
  // Why startup refused to come up clean (orphan recovery or unfinished
  // transactions). Kept separate from last_fallback_, which only describes
  // runtime auto-fallbacks, so a rejected apply can name its actual cause.
  std::string startup_degraded_reason_;
  std::int64_t next_fallback_attempt_ms_ = 0;
  FeatureWindow features_;
  std::int64_t last_feature_ms_ = 0;
  std::int64_t last_refresh_ms_ = 0;
  std::int64_t last_liveness_ms_ = 0;
  int liveness_strikes_ = 0;
  std::int64_t auto_last_check_ms_ = 0;
  std::int64_t auto_last_action_ms_ = 0;
  std::string auto_last_hold_;
  std::string auto_last_reason_;
  std::string auto_last_expert_;
  // Route changes and completed switches, kept so the suppression gates have
  // history to reason about. In-memory only: after a restart the autopilot must
  // be re-handed the lease anyway, so it starts with a clean budget.
  std::vector<std::int64_t> auto_switch_times_;
  std::string auto_desired_;
  std::int64_t auto_desired_since_ms_ = 0;
  // Measured service evidence, advisory only. `adverse_experts_` remembers an
  // expert that a post-switch measurement found outside the declared limits, so
  // the same mistake is not proposed again on the same evidence. Cleared with a
  // restart, exactly like the switch budget.
  Json active_safety_ = Json::object();
  bool has_observation_ = false;
  ServiceObservation observation_;
  std::map<std::string, std::string> adverse_experts_;
  // Recent consecutive window values per expert. Only used to say how finely the
  // measurement can resolve a change, never to approve one.
  std::map<std::string, std::vector<double>> fg_p99_history_;
  std::map<std::string, std::vector<double>> bg_progress_history_;
  bool outcome_pending_ = false;
  int outcome_windows_ = 0;
  // Telemetry pipeline health, counted by the consumer so "lost observations"
  // is a number the control plane reports, not just something the probe knows.
  std::uint64_t observations_accepted_ = 0;
  std::uint64_t observations_rejected_ = 0;
  std::string last_rejection_;
  // "Lost observations" has to survive the moment it happens: a gap is only
  // useful to an operator if the control plane still reports it ten minutes later.
  std::int64_t observation_max_age_ms_ = 0;
  std::uint64_t stale_checks_ = 0;
  ServiceObservation outcome_before_;
  std::string outcome_from_;
  std::string outcome_to_;
  std::string last_outcome_;
  RuleRouter router_;
};

}  // namespace ep
