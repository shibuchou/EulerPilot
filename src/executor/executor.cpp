#include "ep/executor.hpp"

#include <cmath>

#include "ep/autopilot.hpp"
#include "ep/policy.hpp"
#include "ep/target.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <string>
#include <utility>

namespace ep {
namespace {

bool get_string(const Json& object, const char* key, std::size_t min_len,
                std::size_t max_len, std::string* out, std::string* error) {
  if (!object.contains(key) || !object.at(key).is_string()) {
    if (error != nullptr) *error = std::string(key) + " must be a string";
    return false;
  }
  *out = object.at(key).get<std::string>();
  if (out->size() < min_len || out->size() > max_len) {
    if (error != nullptr) {
      *error = std::string(key) + " length must be " + std::to_string(min_len) + ".." +
               std::to_string(max_len);
    }
    return false;
  }
  return true;
}

bool get_u64(const Json& object, const char* key, std::uint64_t min_value,
             std::uint64_t* out, std::string* error) {
  if (!object.contains(key) ||
      (!object.at(key).is_number_integer() && !object.at(key).is_number_unsigned())) {
    if (error != nullptr) *error = std::string(key) + " must be an integer";
    return false;
  }
  const long long value = object.at(key).get<long long>();
  if (value < 0 || static_cast<std::uint64_t>(value) < min_value) {
    if (error != nullptr) *error = std::string(key) + " is below the minimum";
    return false;
  }
  *out = static_cast<std::uint64_t>(value);
  return true;
}

bool is_hex64(const std::string& text) {
  if (text.size() != 64) return false;
  for (char c : text) {
    const bool digit = c >= '0' && c <= '9';
    const bool lower = c >= 'a' && c <= 'f';
    if (!digit && !lower) return false;
  }
  return true;
}

}  // namespace

Executor::Executor(std::filesystem::path state_dir, std::unique_ptr<Skill> skill,
                   ExecutorOptions options)
    : state_dir_(std::move(state_dir)),
      skill_(std::move(skill)),
      options_(options),
      ownership_(state_dir_),
      journal_(state_dir_) {}

Executor::~Executor() {
  running_ = false;
  if (monitor_.joinable()) monitor_.join();
}

void Executor::shutdown(const std::string& reason) {
  running_ = false;
  if (monitor_.joinable()) monitor_.join();
  std::lock_guard<std::mutex> guard(mu_);
  if (!active_) return;
  // The 2s retry backoff must not decide whether we leave an effect in the kernel.
  next_fallback_attempt_ms_ = 0;
  auto_fallback_locked(reason);
}

bool Executor::init(std::string* error) {
  if (!ownership_.open(error)) return false;
  if (!journal_.open(error)) return false;
  if (options_.fail_journal_after >= 0) {
    journal_.set_fault_after(options_.fail_journal_after);
  }
  skill_->configure(SkillContext{ownership_.instance_id()});

  const std::uint64_t journal_generation = journal_.max_committed_generation();
  if (journal_generation > ownership_.generation()) {
    if (!ownership_.commit_generation(journal_generation, error)) return false;
  }

  std::string orphan_error;
  const RecoveryResult orphan = skill_->recover_orphan(&orphan_error);
  if (!orphan.ok) {
    // The skill's detail is the actionable part; the out-parameter is often
    // empty on this path, which used to log "orphan recovery failed: " and
    // nothing else while every later apply came back RECOVERY_REQUIRED.
    startup_degraded_reason_ = orphan.detail.empty() ? orphan_error : orphan.detail;
    if (startup_degraded_reason_.empty()) startup_degraded_reason_ = "orphan recovery failed";
    if (orphan.conflict) startup_degraded_reason_ += "; foreign resources left untouched";
    std::cerr << "[executor] orphan recovery failed: " << startup_degraded_reason_ << "\n";
  }
  std::vector<std::string> unresolved;
  if (!reconcile_all(&unresolved, error)) return false;
  degraded_ = !unresolved.empty() || !orphan.ok;
  for (const auto& txn : unresolved) {
    if (startup_degraded_reason_.empty())
      startup_degraded_reason_ = "unfinished transactions";
    startup_degraded_reason_ += "; " + txn;
    std::cerr << "[executor] unresolved transaction " << txn
              << "; executor starts DEGRADED\n";
  }
  running_ = true;
  monitor_ = std::thread([this] { monitor_loop(); });
  return true;
}

Json Executor::rejection(const std::string& request_id, ErrorCode code,
                         const std::string& message, const std::string& detail) const {
  ActionResult result = make_rejection(request_id, code, message, detail);
  result.observed_generation = ownership_.generation();
  return result.to_json();
}

std::string Executor::request_digest(const std::string& operation,
                                     const Json& payload) const {
  // Idempotency identifies the intent, not the transport state. Lease, fence
  // and expected_generation may change between retries of the same intent.
  return sha256_hex(canonical_json(Json{{"operation", operation}, {"payload", payload}}));
}

Json Executor::handle(const Json& request) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!request.is_object()) return rejection("", ErrorCode::kSchemaInvalid, "request must be an object");
  const std::string operation = request.value("operation", "");
  if (operation == "policy.status") return handle_status(request);
  if (operation == "feature.snapshot") return handle_features(request);
  if (operation == "route.decide") return handle_route(request);
  if (operation == "slo.evaluate") return handle_slo(request);
  if (operation == "service.observe") return handle_observe(request);
  if (operation == "lease.acquire") return handle_lease(request);
  if (operation == "policy.validate") return handle_validate(request);
  if (operation == "policy.apply") return handle_apply(request);
  if (operation == "policy.rollback") return handle_rollback(request);
  if (operation == "transaction.reconcile") return handle_reconcile(request);
  return rejection(request.value("request_id", ""), ErrorCode::kSchemaInvalid,
                   "unknown operation", operation);
}

Json Executor::handle_status(const Json& request) {
  const std::string request_id = request.value("request_id", "");
  Json unfinished = Json::array();
  for (const auto& txn : journal_.transactions_needing_reconcile()) unfinished.push_back(txn);
  Json adverse_json = Json::object();
  for (const auto& entry : adverse_experts_) adverse_json[entry.first] = entry.second;
  const std::int64_t now_ms = now_epoch_ms();
  return Json{{"request_id", request_id},
              {"status", "OK"},
              {"instance_id", ownership_.instance_id()},
              {"skill", skill_->name()},
              {"generation", ownership_.generation()},
              {"degraded", degraded_},
              {"lease", ownership_.lease().to_json()},
              {"journal_records", journal_.size()},
              {"last_fallback", last_fallback_},
              {"store_warning", last_store_warning_},
              {"startup_degraded_reason", startup_degraded_reason_},
              {"auto_apply", Json{{"enabled", options_.auto_apply},
                                  {"owner", options_.auto_owner},
                                  {"baseline", options_.auto_baseline_path},
                                  {"active_expert",
                                   active_ ? active_->policy_expert : std::string()},
                                  {"last_expert", auto_last_expert_},
                                  {"last_reason", auto_last_reason_},
                                  {"last_hold", auto_last_hold_},
                                  {"last_action_at_ms", auto_last_action_ms_},
                                  {"min_interval_ms", options_.auto_min_interval_ms},
                                  {"dwell_ms", options_.auto_dwell_ms},
                                  {"candidate_hold_ms", options_.auto_candidate_hold_ms},
                                  {"budget_window_ms", options_.auto_budget_window_ms},
                                  {"budget_max_switches", options_.auto_budget_max_switches},
                                  {"switches_recorded",
                                   static_cast<std::int64_t>(auto_switch_times_.size())},
                                  {"projected_unprotected_ms",
                                   static_cast<std::int64_t>(auto_switch_times_.size()) *
                                       options_.auto_measured_downtime_ms},
                                  {"measured_downtime_ms", options_.auto_measured_downtime_ms}}},
              {"service", Json{{"observed", has_observation_},
                               {"workload", observation_.workload},
                               {"measured_expert", observation_.expert},
                               {"observed_at_ms", observation_.observed_at_ms},
                               {"window_ms", observation_.window_ms},
                               {"age_ms", has_observation_ ? now_ms - observation_.observed_at_ms
                                                            : std::int64_t{0}},
                               {"foreground_p99_ms", observation_.has_foreground_p99
                                                         ? Json(observation_.foreground_p99_ms)
                                                         : Json(nullptr)},
                               {"bg_work_units_per_sec",
                                observation_.has_bg_work_units
                                    ? Json(observation_.bg_work_units_per_sec)
                                    : Json(nullptr)},
                               {"bg_max_wait_ms", observation_.has_bg_max_wait
                                                      ? Json(observation_.bg_max_wait_ms)
                                                      : Json(nullptr)},
                               {"declared_limits", active_safety_},
                               {"adverse_experts", adverse_json},
                               {"outcome_pending", outcome_pending_},
                               {"outcome_windows", outcome_windows_},
                               {"outcome_windows_required", options_.auto_outcome_windows},
                               {"observations_accepted", observations_accepted_},
                               {"observations_rejected", observations_rejected_},
                               {"last_rejection", last_rejection_},
                               {"observation_max_age_ms", observation_max_age_ms_},
                               {"stale_checks", stale_checks_},
                               {"noise_p99_pct", service_noise(observation_.expert).p99_pct},
                               {"noise_bg_progress_pct",
                                service_noise(observation_.expert).bg_progress_pct},
                               {"last_outcome", last_outcome_}}},
              {"incomplete_transactions", unfinished}};
}

Json Executor::handle_features(const Json& request) {
  Json snapshot = features_.snapshot();
  snapshot["request_id"] = request.value("request_id", "");
  return snapshot;
}

Json Executor::handle_route(const Json& request) {
  Json decision = router_.decide(features_.snapshot(), now_epoch_ms());
  decision["request_id"] = request.value("request_id", "");
  return decision;
}

SloInput Executor::service_slo_input(const std::string& expert, std::int64_t now_ms) {
  SloInput in;
  in.features = features_.snapshot();
  in.expert = expert;
  in.now_ms = now_ms;
  in.max_observation_age_ms = 1000;
  in.safety = active_safety_;
  in.has_observation = has_observation_;
  in.observation = observation_;
  in.adverse = adverse_experts_;
  if (has_observation_) {
    const std::int64_t age = now_ms - observation_.observed_at_ms;
    if (age > observation_max_age_ms_) observation_max_age_ms_ = age;
    if (age > 2 * observation_.window_ms) stale_checks_++;
  }
  return in;
}

Json Executor::handle_slo(const Json& request) {
  const std::string expert = request.value("expert", "");
  SloInput in = service_slo_input(expert, now_epoch_ms());
  in.max_observation_age_ms = request.value("max_observation_age_ms", 1000LL);
  Json decision = SloGate{}.evaluate(in);
  decision["request_id"] = request.value("request_id", "");
  return decision;
}

namespace {

// Largest change between consecutive windows of the same expert, as a percentage.
// Two windows is enough to say "this signal jitters by at least X"; one is not.
void remember(std::vector<double>& history, bool present, double value) {
  if (!present) return;
  if (history.size() >= 8) history.erase(history.begin());
  history.push_back(value);
}

// A pair of consecutive windows only shows jitter if the two describe the same
// world. SP3 showed why that needs saying: the same档 measured 1.9ms under load
// and 0.13ms after it, and counting that as "noise" would make the guard refuse
// everything forever. Differences beyond a factor of four are state changes.
double jitter_pct(const std::vector<double>& values) {
  double worst = 0.0;
  for (std::size_t i = 1; i < values.size(); ++i) {
    const double previous = values[i - 1];
    const double current = values[i];
    if (previous <= 0.0 || current <= 0.0) continue;
    const double ratio = current > previous ? current / previous : previous / current;
    if (ratio > 4.0) continue;
    const double delta = std::abs(current - previous) / previous * 100.0;
    if (delta > worst) worst = delta;
  }
  return worst;
}

}  // namespace

ServiceNoise Executor::service_noise(const std::string& expert) const {
  ServiceNoise noise;
  const auto fg = fg_p99_history_.find(expert);
  if (fg != fg_p99_history_.end()) noise.p99_pct = jitter_pct(fg->second);
  const auto bg = bg_progress_history_.find(expert);
  if (bg != bg_progress_history_.end()) noise.bg_progress_pct = jitter_pct(bg->second);
  return noise;
}

Json Executor::handle_observe(const Json& request) {
  const std::string request_id = request.value("request_id", "");
  ServiceObservation parsed;
  std::string error;
  if (!request.contains("observation") ||
      !parse_service_observation(request.at("observation"), &parsed, &error)) {
    observations_rejected_++;
    last_rejection_ = error;
    return rejection(request_id, ErrorCode::kSchemaInvalid,
                     "service observation rejected", error);
  }
  const std::int64_t now_ms = now_epoch_ms();
  if (parsed.observed_at_ms < now_ms - 600000 ||
      parsed.observed_at_ms < now_ms - 10 * parsed.window_ms) {
    // Accepting a long-dead window would silently replace a fresher one and then
    // sit there denying every change as stale, which reads like a gate fault.
    observations_rejected_++;
    last_rejection_ = "observation is too old to describe the present";
    return rejection(request_id, ErrorCode::kSchemaInvalid,
                     "service observation is too old",
                     "observed_at_ms=" + std::to_string(parsed.observed_at_ms) +
                         " now_ms=" + std::to_string(now_ms));
  }
  if (parsed.observed_at_ms > now_ms + 5000) {
    observations_rejected_++;
    last_rejection_ = "observation is from the future";
    return rejection(request_id, ErrorCode::kSchemaInvalid, "service observation is from the future",
                     "observed_at_ms=" + std::to_string(parsed.observed_at_ms) +
                         " now_ms=" + std::to_string(now_ms));
  }

  if (!outcome_pending_) {
    remember(fg_p99_history_[parsed.expert], parsed.has_foreground_p99, parsed.foreground_p99_ms);
    remember(bg_progress_history_[parsed.expert], parsed.has_bg_work_units,
             parsed.bg_work_units_per_sec);
  }

  Json outcome = Json::object();
  if (outcome_pending_ && parsed.expert == outcome_to_) {
    const ServiceNoise noise = service_noise(outcome_from_);
    const OutcomeVerdict verdict = evaluate_switch_outcome(active_safety_, outcome_before_, parsed,
                                                          noise);
    outcome = Json{{"from", outcome_from_},
                   {"to", outcome_to_},
                   {"evaluated", true},
                   {"windows_observed", outcome_windows_ + 1},
                   {"windows_required", options_.auto_outcome_windows},
                   {"noise_p99_pct", noise.p99_pct},
                   {"noise_bg_progress_pct", noise.bg_progress_pct}};
    if (verdict.code.empty() && (verdict.skipped_foreground || verdict.skipped_ratio)) {
      // The contract asks for a resolution the measurement does not have, so the
      // comparison is reported as unrunnable instead of silently passing.
      outcome["hurt"] = false;
      outcome["comparable"] = false;
      outcome["detail"] = verdict.detail;
      last_outcome_ = outcome_to_ + ": " + verdict.detail;
      outcome_pending_ = false;
      outcome_windows_ = 0;
      std::cerr << "[executor] " << last_outcome_ << "\n";
    } else if (verdict.code.empty()) {
      // One clean window is enough to stop treating the change as damaging.
      last_outcome_ = outcome_to_ + " measured within the declared limits";
      outcome_pending_ = false;
      outcome_windows_ = 0;
      outcome["hurt"] = false;
      outcome["comparable"] = true;
    } else {
      outcome_windows_++;
      outcome["comparable"] = true;
      outcome["code"] = verdict.code;
      outcome["detail"] = verdict.detail;
      if (outcome_windows_ >= options_.auto_outcome_windows) {
        // The expert that produced this measurement is now known-bad on this
        // evidence, so the gate refuses to propose it again until the service
        // measurement says otherwise.
        adverse_experts_[outcome_to_] = verdict.code + ": " + verdict.detail + " (" +
                                        std::to_string(outcome_windows_) +
                                        " consecutive windows)";
        last_outcome_ = outcome_to_ + " regressed the service: " + verdict.code + " (" +
                        verdict.detail + ")";
        std::cerr << "[executor] service evidence against " << outcome_to_ << ": " << verdict.code
                  << " (" << verdict.detail << ") over " << outcome_windows_ << " windows\n";
        outcome["hurt"] = true;
        outcome_pending_ = false;
        outcome_windows_ = 0;
      } else {
        // A p99 from one window moves by more than the declared budget on noise
        // alone, so a single violation is recorded as suspicion, not evidence.
        outcome["hurt"] = false;
        last_outcome_ = "suspected " + verdict.code + " in window " +
                        std::to_string(outcome_windows_) + " of " +
                        std::to_string(options_.auto_outcome_windows) + " for " + outcome_to_;
        std::cerr << "[executor] " << last_outcome_ << "; confirming\n";
      }
    }
  } else if (outcome_pending_) {
    outcome = Json{{"from", outcome_from_},
                   {"to", outcome_to_},
                   {"evaluated", false},
                   {"detail", "measurement is labelled " + parsed.expert + ", still waiting for " +
                                  outcome_to_}};
  }

  observation_ = parsed;
  has_observation_ = true;
  observations_accepted_++;
  Json adverse_json = Json::object();
  for (const auto& entry : adverse_experts_) adverse_json[entry.first] = entry.second;
  return Json{{"status", "OK"},
              {"request_id", request_id},
              {"accepted", Json{{"workload", parsed.workload},
                                {"expert", parsed.expert},
                                {"observed_at_ms", parsed.observed_at_ms},
                                {"window_ms", parsed.window_ms}}},
              {"observed_generation", ownership_.generation()},
              {"switch_outcome", outcome},
              {"adverse_experts", adverse_json}};
}

Json Executor::handle_lease(const Json& request) {
  std::string request_id = request.value("request_id", "");
  if (request_id.empty()) request_id = "req-" + random_hex(8);
  std::string owner;
  std::string error;
  if (request.contains("lease_owner")) {
    if (!get_string(request, "lease_owner", 8, 128, &owner, &error)) {
      return rejection(request_id, ErrorCode::kSchemaInvalid, error);
    }
  } else {
    owner = "agent-" + random_hex(6);
  }
  std::uint64_t ttl_ms = 30000;
  if (request.contains("lease_ttl_ms") &&
      !get_u64(request, "lease_ttl_ms", 1000, &ttl_ms, &error)) {
    return rejection(request_id, ErrorCode::kSchemaInvalid, error);
  }
  LeaseState lease;
  bool persisted = true;
  if (!ownership_.acquire_lease(owner, ttl_ms, &lease, &error, &persisted)) {
    if (!persisted) {
      // The store is unwritable (disk full, read-only mount, ...). A lease that only lives
      // in this process must not back a *new* effect, but refusing it altogether would block
      // the one action that is always safe: taking an existing effect back down to fair.
      // Grant it, say out loud that it is not durable, and let the degraded flag refuse
      // applies until someone reconciles.
      degraded_ = true;
      last_store_warning_ = "lease is in memory only: " + error;
      std::cerr << "[executor] lease granted in memory only: " << error << "\n";
      return Json{{"request_id", request_id},
                  {"status", "OK"},
                  {"lease_owner", lease.owner},
                  {"fence_epoch", lease.fence},
                  {"expires_ms", lease.expires_ms},
                  {"expires_at", iso8601_from_ms(lease.expires_ms)},
                  {"observed_generation", ownership_.generation()},
                  {"lease_persisted", false},
                  {"warning", last_store_warning_ +
                                  "; policy.apply is refused until reconcile succeeds"}};
    }
    return rejection(request_id, ErrorCode::kSchemaInvalid, error);
  }
  return Json{{"request_id", request_id},
              {"status", "OK"},
              {"lease_owner", lease.owner},
              {"fence_epoch", lease.fence},
              {"expires_ms", lease.expires_ms},
              {"expires_at", iso8601_from_ms(lease.expires_ms)},
              {"observed_generation", ownership_.generation()}};
}

bool Executor::parse_envelope(const Json& value, bool require_rollback,
                              Envelope* out, std::string* error) const {
  if (!value.is_object()) {
    if (error != nullptr) *error = "envelope must be an object";
    return false;
  }
  Envelope env;
  std::string api_version;
  if (!get_string(value, "apiVersion", 1, 64, &api_version, error)) return false;
  if (api_version != kApiVersion) {
    if (error != nullptr) *error = "unsupported apiVersion: " + api_version;
    return false;
  }
  if (!get_string(value, "request_id", 8, 128, &env.request_id, error)) return false;
  if (!get_string(value, "transaction_id", 8, 128, &env.transaction_id, error)) return false;
  if (!get_string(value, "idempotency_key", 8, 128, &env.idempotency_key, error)) return false;
  if (!get_string(value, "operation", 1, 64, &env.operation, error)) return false;
  if (!get_string(value, "policy_sha256", 64, 64, &env.policy_sha256, error)) return false;
  if (!is_hex64(env.policy_sha256)) {
    if (error != nullptr) *error = "policy_sha256 must be 64 lowercase hex characters";
    return false;
  }
  if (!get_string(value, "lease_owner", 8, 128, &env.lease_owner, error)) return false;
  if (!get_u64(value, "fence_epoch", 1, &env.fence_epoch, error)) return false;
  if (!get_u64(value, "expected_generation", 0, &env.expected_generation, error)) {
    return false;
  }
  std::string issued_at;
  std::string deadline;
  if (!get_string(value, "issued_at", 20, 64, &issued_at, error)) return false;
  if (!get_string(value, "deadline", 20, 64, &deadline, error)) return false;
  if (!parse_iso8601_ms(issued_at, &env.issued_ms)) {
    if (error != nullptr) *error = "issued_at is not a valid RFC 3339 timestamp";
    return false;
  }
  if (!parse_iso8601_ms(deadline, &env.deadline_ms)) {
    if (error != nullptr) *error = "deadline is not a valid RFC 3339 timestamp";
    return false;
  }
  if (env.deadline_ms <= env.issued_ms) {
    if (error != nullptr) *error = "deadline must be after issued_at";
    return false;
  }
  if (require_rollback) {
    if (!value.contains("rollback_of") || !value.at("rollback_of").is_object()) {
      if (error != nullptr) *error = "rollback_of is required for policy.rollback";
      return false;
    }
    const Json& rollback = value.at("rollback_of");
    std::string action_id;
    std::string original_txn;
    std::string snapshot_id;
    if (!get_string(rollback, "apply_action_id", 8, 128, &action_id, error)) return false;
    if (!get_string(rollback, "original_transaction_id", 8, 128, &original_txn, error)) {
      return false;
    }
    if (!get_string(rollback, "snapshot_id", 8, 128, &snapshot_id, error)) return false;
    env.rollback_of = Json{{"apply_action_id", action_id},
                           {"original_transaction_id", original_txn},
                           {"snapshot_id", snapshot_id}};
  }
  *out = std::move(env);
  return true;
}

Json Executor::compensate(const Envelope& envelope, const std::string& action_id,
                          const Snapshot& snapshot, const Applied& applied,
                          const std::string& request_hash, const std::string& cause,
                          ErrorCode code) {
  std::string error;
  JournalRecord step;
  step.txn = envelope.transaction_id;
  step.action_id = action_id;
  step.type = "COMPENSATING";
  step.generation = ownership_.generation();
  step.owner = envelope.lease_owner;
  step.fence = envelope.fence_epoch;
  step.data = Json{{"cause", cause}};
  journal_.append(step, &error);

  const RecoveryResult recovery = skill_->rollback(applied, snapshot, &error);
  ActionResult result;
  result.request_id = envelope.request_id;
  result.transaction_id = envelope.transaction_id;
  result.action_id = action_id;
  result.snapshot_id = snapshot.id;
  result.policy_sha256 = envelope.policy_sha256;
  result.observed_generation = ownership_.generation();
  result.rollback.attempted = true;

  if (recovery.ok) {
    // The effect is gone; release it. If the audit record cannot be written we
    // keep the executor degraded instead of pretending a complete rollback.
    if (active_ && active_->action_id == action_id) active_.reset();
    JournalRecord rolled_back = step;
    rolled_back.type = "ROLLED_BACK";
    rolled_back.data = Json{{"cause", cause},
                            {"compensated", true},
                            {"detail", recovery.detail},
                            {"generation_advanced", false}};
    if (!journal_.append(rolled_back, &error)) {
      degraded_ = true;
      result.status = Status::kDegraded;
      result.rollback.status = "AUDIT_PENDING";
      result.rollback.detail = recovery.detail + "; audit pending: " + error;
      result.error = Error{code, cause + "; effect restored, audit pending", error};
    } else {
      result.status = Status::kRolledBack;
      result.rollback.status = "ROLLED_BACK";
      result.rollback.detail = recovery.detail;
      result.error = Error{code, cause, recovery.detail};
    }
  } else {
    degraded_ = true;
    JournalRecord recovery_required = step;
    recovery_required.type = "RECOVERY_REQUIRED";
    recovery_required.data = Json{{"cause", cause},
                                  {"detail", recovery.detail},
                                  {"conflict", recovery.conflict}};
    journal_.append(recovery_required, &error);
    result.status = Status::kRecoveryRequired;
    result.rollback.status = "FAILED";
    result.rollback.detail = recovery.detail;
    result.error = Error{ErrorCode::kRollbackFailed, cause, error};
  }

  const Json response = result.to_json();
  journal_.append_result(envelope.idempotency_key, request_hash, response, &error);
  return response;
}

Json Executor::handle_validate(const Json& request) {
  const std::string request_id = request.value("request_id", "");
  if (!request.contains("policy")) {
    return rejection(request_id, ErrorCode::kSchemaInvalid, "missing policy");
  }
  const Json& policy = request.at("policy");
  PolicySummary summary;
  std::string error;
  if (!validate_scheduling_policy(policy, &summary, &error)) {
    return rejection(request_id, ErrorCode::kSchemaInvalid, "policy validation failed", error);
  }
  std::vector<TargetBinding> bindings;
  if (!resolve_targets(summary.targets, &bindings, &error)) {
    return rejection(request_id, ErrorCode::kTargetStale, "target resolution failed", error);
  }
  Json array = Json::array();
  for (const auto& binding : bindings) array.push_back(binding.to_json());
  return Json{{"request_id", request_id},
              {"status", "OK"},
              {"valid", true},
              {"policy_sha256", sha256_hex(canonical_json(policy))},
              {"name", summary.name},
              {"mode", summary.mode},
              {"expert", summary.expert},
              {"source", summary.source},
              {"target_bindings", array}};
}

Json Executor::handle_apply(const Json& request) {
  std::string request_id = request.value("request_id", "");
  if (!request.contains("envelope")) {
    return rejection(request_id, ErrorCode::kSchemaInvalid, "missing envelope");
  }
  Envelope envelope;
  std::string error;
  if (!parse_envelope(request.at("envelope"), false, &envelope, &error)) {
    return rejection(request_id, ErrorCode::kSchemaInvalid, error);
  }
  request_id = envelope.request_id;
  if (envelope.operation != "policy.apply") {
    return rejection(request_id, ErrorCode::kSchemaInvalid,
                     "operation must be policy.apply");
  }
  const char* payload_key = options_.test_mode ? "intent" : "policy";
  if (!request.contains(payload_key)) {
    return rejection(request_id, ErrorCode::kSchemaInvalid,
                     std::string("missing ") + payload_key);
  }
  const Json payload = request.at(payload_key);
  const std::string digest = sha256_hex(canonical_json(payload));
  if (digest != envelope.policy_sha256) {
    return rejection(request_id, ErrorCode::kSchemaInvalid,
                     "policy_sha256 does not match the canonical payload digest");
  }
  const std::string request_hash = request_digest("policy.apply", payload);

  if (auto existing = journal_.find_result(envelope.idempotency_key)) {
    if (existing->data.value("request_sha256", "") != request_hash) {
      return rejection(request_id, ErrorCode::kIdempotencyConflict,
                       "same idempotency_key with a different request");
    }
    return existing->data.at("response");
  }

  PolicySummary policy_summary;
  if (!options_.test_mode) {
    if (!validate_scheduling_policy(payload, &policy_summary, &error)) {
      return rejection(request_id, ErrorCode::kSchemaInvalid, "policy validation failed", error);
    }
  }
  if (now_epoch_ms() > envelope.deadline_ms) {
    return rejection(request_id, ErrorCode::kDeadlineExceeded, "deadline has passed");
  }
  if (!ownership_.validate_lease(envelope.lease_owner, envelope.fence_epoch, &error)) {
    return rejection(request_id, ErrorCode::kOwnershipConflict, "lease/fence rejected", error);
  }
  if (envelope.expected_generation != ownership_.generation()) {
    return rejection(request_id, ErrorCode::kGenerationConflict,
                     "expected_generation does not match the executor generation",
                     "expected " + std::to_string(envelope.expected_generation) + ", current " +
                         std::to_string(ownership_.generation()));
  }
  if (degraded_) {
    ActionResult result;
    result.request_id = request_id;
    result.status = Status::kRecoveryRequired;
    result.observed_generation = ownership_.generation();
    result.error = Error{ErrorCode::kRecoveryRequired,
                         "executor is degraded; reconcile before applying",
                         startup_degraded_reason_.empty() ? "run transaction.reconcile"
                                                          : startup_degraded_reason_};
    return result.to_json();
  }
  if (journal_.broken()) {
    ActionResult result;
    result.request_id = request_id;
    result.status = Status::kRecoveryRequired;
    result.observed_generation = ownership_.generation();
    result.error = Error{ErrorCode::kRecoveryRequired,
                         "journal is broken; restart and reconcile before applying",
                         "journal append failed; effects may need recovery"};
    return result.to_json();
  }

  // Generated proposals must pass the SLO gate. A human-explicit policy keeps
  // operator authority and bypasses the gate.
  if (!options_.test_mode && policy_summary.source != "human") {
    // Judge the proposal against the limits it declares for itself, plus any
    // measured service evidence already on file.
    SloInput proposal_gate_input = service_slo_input(policy_summary.expert, now_epoch_ms());
    proposal_gate_input.safety = policy_summary.safety;
    const Json gate = SloGate{}.evaluate(proposal_gate_input);
    if (!gate.value("allowed", false)) {
      return rejection(request_id, ErrorCode::kSloDenied,
                       "SLO gate denied expert '" + policy_summary.expert + "' from source '" +
                           policy_summary.source + "'",
                       gate.dump());
    }
  }

  const SkillProbe probe = skill_->probe();
  if (!probe.ok) {
    return rejection(request_id, ErrorCode::kUnsupported, "skill probe failed", probe.detail);
  }
  Plan plan;
  if (!skill_->plan(payload, &plan, &error)) {
    return rejection(request_id, ErrorCode::kSchemaInvalid, "plan rejected", error);
  }
  std::vector<TargetBinding> bindings;
  if (!options_.test_mode) {
    if (!resolve_targets(policy_summary.targets, &bindings, &error)) {
      return rejection(request_id, ErrorCode::kTargetStale, "target resolution failed", error);
    }
  }
  Snapshot snapshot;
  if (!skill_->snapshot(plan, &snapshot, &error)) {
    return rejection(request_id, ErrorCode::kInternal, "snapshot failed", error);
  }
  if (!bindings.empty() && !revalidate_targets(bindings, &error)) {
    return rejection(request_id, ErrorCode::kTargetStale, "target binding became stale", error);
  }
  if (now_epoch_ms() > envelope.deadline_ms) {
    return rejection(request_id, ErrorCode::kDeadlineExceeded,
                     "deadline passed before the apply was journaled");
  }
  Json target_array = Json::array();
  for (const auto& binding : bindings) target_array.push_back(binding.to_json());

  const std::string action_id = "act-" + random_hex(12);
  JournalRecord prepared;
  prepared.txn = envelope.transaction_id;
  prepared.action_id = action_id;
  prepared.type = "PREPARED";
  prepared.generation = ownership_.generation();
  prepared.owner = envelope.lease_owner;
  prepared.fence = envelope.fence_epoch;
  prepared.data = Json{{"operation", "policy.apply"},
                       {"policy_sha256", digest},
                       {"snapshot_id", snapshot.id},
                       {"snapshot", snapshot.data},
                       {"intent", payload},
                       {"plan", plan.params},
                       {"target_bindings", target_array},
                       {"generation_before", ownership_.generation()}};
  if (!journal_.append(prepared, &error)) {
    return rejection(request_id, ErrorCode::kInternal, "journal PREPARED failed", error);
  }

  JournalRecord step = prepared;
  step.data = Json::object();
  step.type = "APPLYING";
  if (!journal_.append(step, &error)) {
    return rejection(request_id, ErrorCode::kInternal, "journal APPLYING failed", error);
  }

  Applied applied;
  if (!skill_->apply(plan, snapshot, ownership_.lease(), &applied, &error)) {
    return compensate(envelope, action_id, snapshot, applied, request_hash, error,
                      ErrorCode::kApplyFailed);
  }
  // The effect exists from here on. Track it before any journal/state write so
  // a later persistence failure cannot lose the recovery context.
  applied.bindings = bindings;
  const std::string previous_expert = active_ ? active_->policy_expert : std::string();
  const bool had_observation_before = has_observation_;
  const ServiceObservation observation_before = observation_;
  active_ = ActiveAction{envelope.transaction_id, action_id, envelope.lease_owner,
                         envelope.fence_epoch, applied.data, snapshot.data,
                         snapshot.id, digest, policy_summary.expert, now_epoch_ms(),
                         policy_summary.safety.value("policy_ttl_ms", 60000LL),
                         applied.bindings};
  if (now_epoch_ms() > envelope.deadline_ms) {
    return compensate(envelope, action_id, snapshot, applied, request_hash,
                      "deadline exceeded after skill.apply", ErrorCode::kDeadlineExceeded);
  }

  step.type = "VERIFYING";
  if (!journal_.append(step, &error)) {
    return compensate(envelope, action_id, snapshot, applied, request_hash,
                      "journal VERIFYING failed: " + error, ErrorCode::kInternal);
  }
  const VerifyResult verify = skill_->verify(applied, envelope.deadline_ms);
  if (!verify.ok) {
    return compensate(envelope, action_id, snapshot, applied, request_hash, verify.detail,
                      ErrorCode::kVerifyFailed);
  }
  if (now_epoch_ms() > envelope.deadline_ms) {
    return compensate(envelope, action_id, snapshot, applied, request_hash,
                      "deadline exceeded after skill.verify",
                      ErrorCode::kDeadlineExceeded);
  }

  JournalRecord verified = step;
  verified.type = "VERIFIED";
  verified.data = Json{{"applied", applied.data}, {"detail", verify.detail}};
  if (!journal_.append(verified, &error)) {
    return compensate(envelope, action_id, snapshot, applied, request_hash,
                      "journal VERIFIED failed: " + error, ErrorCode::kInternal);
  }

  const std::uint64_t new_generation = ownership_.generation() + 1;
  JournalRecord committed = step;
  committed.type = "COMMITTED";
  committed.generation = new_generation;
  committed.data = Json{{"snapshot_id", snapshot.id}, {"applied", applied.data}};
  if (!journal_.append(committed, &error)) {
    return compensate(envelope, action_id, snapshot, applied, request_hash,
                      "journal COMMITTED failed: " + error, ErrorCode::kInternal);
  }

  ActionResult result;
  result.request_id = request_id;
  result.transaction_id = envelope.transaction_id;
  result.action_id = action_id;
  result.snapshot_id = snapshot.id;
  result.status = Status::kVerified;
  result.observed_generation = new_generation;
  result.policy_sha256 = digest;
  result.checks.push_back(Check{"probe", true, probe.detail});
  result.checks.push_back(Check{"plan", true, "params prepared"});
  if (!options_.test_mode) {
    result.checks.push_back(Check{
        "policy", true, policy_summary.name + "/" + policy_summary.mode + "/" +
                             policy_summary.expert});
    result.checks.push_back(Check{
        "targets", true, std::to_string(bindings.size()) + " cgroup binding(s)"});
    for (const auto& binding : bindings) result.target_bindings.push_back(binding.to_json());
  }
  result.checks.push_back(Check{"snapshot", true, snapshot.id});
  result.checks.push_back(Check{"apply", true, "applied"});
  result.checks.push_back(Check{"verify", true, verify.detail});

  // The gate reads the limits this policy declares, so they only become binding
  // once the policy is actually in place.
  active_safety_ = policy_summary.safety;
  if (!previous_expert.empty() && previous_expert != policy_summary.expert) {
    // A change of expert is an experiment on the served workload. Arm the outcome
    // check so the next measurement under the new expert decides whether it hurt;
    // without a pre-switch measurement there is nothing to compare against, and
    // that is reported instead of assumed.
    outcome_from_ = previous_expert;
    outcome_to_ = policy_summary.expert;
    outcome_before_ = observation_before;
    outcome_pending_ = had_observation_before;
    outcome_windows_ = 0;
    if (!had_observation_before) {
      last_outcome_ = "no pre-switch service measurement; outcome of " + previous_expert +
                      " -> " + policy_summary.expert + " not evaluated";
    }
  }

  const Json response = result.to_json();
  if (!ownership_.commit_generation(new_generation, &error)) {
    degraded_ = true;
    ActionResult degraded = result;
    degraded.status = Status::kDegraded;
    degraded.error = Error{ErrorCode::kInternal,
                           "state committed but state.json persist failed",
                           error};
    const Json degraded_response = degraded.to_json();
    journal_.append_result(envelope.idempotency_key, request_hash, degraded_response, &error);
    return degraded_response;
  }
  if (!journal_.append_result(envelope.idempotency_key, request_hash, response, &error)) {
    degraded_ = true;
    ActionResult degraded = result;
    degraded.status = Status::kDegraded;
    degraded.error = Error{ErrorCode::kInternal,
                           "state committed but idempotency record failed",
                           error};
    return degraded.to_json();
  }
  return response;
}

Json Executor::handle_rollback(const Json& request) {
  std::string request_id = request.value("request_id", "");
  if (!request.contains("envelope")) {
    return rejection(request_id, ErrorCode::kSchemaInvalid, "missing envelope");
  }
  Envelope envelope;
  std::string error;
  if (!parse_envelope(request.at("envelope"), true, &envelope, &error)) {
    return rejection(request_id, ErrorCode::kSchemaInvalid, error);
  }
  request_id = envelope.request_id;
  if (envelope.operation != "policy.rollback") {
    return rejection(request_id, ErrorCode::kSchemaInvalid,
                     "operation must be policy.rollback");
  }
  const std::string request_hash =
      sha256_hex(canonical_json(Json{{"operation", "policy.rollback"},
                                      {"policy_sha256", envelope.policy_sha256},
                                      {"rollback_of", envelope.rollback_of}}));
  if (auto existing = journal_.find_result(envelope.idempotency_key)) {
    if (existing->data.value("request_sha256", "") != request_hash) {
      return rejection(request_id, ErrorCode::kIdempotencyConflict,
                       "same idempotency_key with a different request");
    }
    return existing->data.at("response");
  }
  if (now_epoch_ms() > envelope.deadline_ms) {
    return rejection(request_id, ErrorCode::kDeadlineExceeded, "deadline has passed");
  }
  if (!ownership_.validate_lease(envelope.lease_owner, envelope.fence_epoch, &error)) {
    return rejection(request_id, ErrorCode::kOwnershipConflict, "lease/fence rejected", error);
  }
  if (envelope.expected_generation != ownership_.generation()) {
    return rejection(request_id, ErrorCode::kGenerationConflict,
                     "expected_generation does not match the executor generation");
  }

  const std::string& original_txn =
      envelope.rollback_of.at("original_transaction_id").get<std::string>();
  const std::string& apply_action_id =
      envelope.rollback_of.at("apply_action_id").get<std::string>();
  const std::string& snapshot_id =
      envelope.rollback_of.at("snapshot_id").get<std::string>();

  const JournalRecord* committed = nullptr;
  const JournalRecord* prepared = nullptr;
  const JournalRecord* verified = nullptr;
  for (const auto& record : journal_.records()) {
    if (record.txn != original_txn) continue;
    if (record.type == "COMMITTED" && record.action_id == apply_action_id) {
      committed = &record;
    } else if (record.type == "PREPARED") {
      prepared = &record;
    } else if (record.type == "VERIFIED") {
      verified = &record;
    }
  }
  if (committed == nullptr || prepared == nullptr || verified == nullptr) {
    return rejection(request_id, ErrorCode::kTargetStale,
                     "original committed transaction not found or incomplete");
  }
  if (prepared->data.value("snapshot_id", "") != snapshot_id) {
    return rejection(request_id, ErrorCode::kSchemaInvalid,
                     "snapshot_id does not belong to the original action");
  }
  if (prepared->data.value("policy_sha256", "") != envelope.policy_sha256) {
    return rejection(request_id, ErrorCode::kSchemaInvalid,
                     "policy_sha256 does not match the original action");
  }
  if (committed->generation != ownership_.generation()) {
    return rejection(request_id, ErrorCode::kGenerationConflict,
                     "later policy changes exist; rollback would discard them",
                     "committed generation " + std::to_string(committed->generation) +
                         ", current " + std::to_string(ownership_.generation()));
  }

  Snapshot snapshot;
  snapshot.id = snapshot_id;
  snapshot.data = prepared->data.at("snapshot");
  Applied applied;
  applied.data = verified->data.at("applied");

  JournalRecord step;
  step.txn = envelope.transaction_id;
  step.action_id = "rbk-" + random_hex(12);
  step.type = "ROLLBACK_PREPARED";
  step.generation = ownership_.generation();
  step.owner = envelope.lease_owner;
  step.fence = envelope.fence_epoch;
  step.data = Json{{"rollback_of", envelope.rollback_of},
                   {"policy_sha256", envelope.policy_sha256},
                   {"snapshot", snapshot.data},
                   {"applied", applied.data},
                   {"generation_before", ownership_.generation()}};
  const bool audit_ok = journal_.append(step, &error);
  if (!audit_ok) {
    degraded_ = true;
    std::cerr << "[executor] ROLLBACK_PREPARED append failed: " << error << "\n";
  }

  const RecoveryResult recovery = skill_->rollback(applied, snapshot, &error);
  if (!recovery.ok) {
    degraded_ = true;
    JournalRecord failed = step;
    failed.type = "RECOVERY_REQUIRED";
    failed.data = Json{{"detail", recovery.detail}, {"conflict", recovery.conflict}};
    journal_.append(failed, &error);
    ActionResult result;
    result.request_id = request_id;
    result.transaction_id = envelope.transaction_id;
    result.action_id = step.action_id;
    result.snapshot_id = snapshot_id;
    result.status = Status::kRecoveryRequired;
    result.observed_generation = ownership_.generation();
    result.policy_sha256 = envelope.policy_sha256;
    result.rollback = RollbackInfo{true, "FAILED", recovery.detail};
    result.error = Error{ErrorCode::kRollbackFailed, "rollback failed", recovery.detail};
    const Json response = result.to_json();
    journal_.append_result(envelope.idempotency_key, request_hash, response, &error);
    return response;
  }

  const VerifyResult restored = skill_->verify_rolled_back(snapshot, envelope.deadline_ms);
  if (!restored.ok) {
    degraded_ = true;
    JournalRecord failed = step;
    failed.type = "RECOVERY_REQUIRED";
    failed.data = Json{{"detail", restored.detail}, {"conflict", true}};
    journal_.append(failed, &error);
    ActionResult result;
    result.request_id = request_id;
    result.transaction_id = envelope.transaction_id;
    result.action_id = step.action_id;
    result.snapshot_id = snapshot_id;
    result.status = Status::kRecoveryRequired;
    result.observed_generation = ownership_.generation();
    result.policy_sha256 = envelope.policy_sha256;
    result.rollback = RollbackInfo{true, "VERIFY_FAILED", restored.detail};
    result.error = Error{ErrorCode::kVerifyFailed, "post-rollback verification failed",
                         restored.detail};
    const Json response = result.to_json();
    journal_.append_result(envelope.idempotency_key, request_hash, response, &error);
    return response;
  }

  const std::uint64_t new_generation = ownership_.generation() + 1;
  JournalRecord completed = step;
  completed.type = "ROLLED_BACK";
  completed.generation = new_generation;
  completed.data = Json{{"rollback_of", envelope.rollback_of},
                        {"snapshot_id", snapshot_id},
                        {"generation_advanced", true},
                        {"detail", recovery.detail}};
  ActionResult result;
  result.request_id = request_id;
  result.transaction_id = envelope.transaction_id;
  result.action_id = step.action_id;
  result.snapshot_id = snapshot_id;
  result.status = Status::kRolledBack;
  result.observed_generation = new_generation;
  result.policy_sha256 = envelope.policy_sha256;
  result.rollback = RollbackInfo{true, "ROLLED_BACK", recovery.detail};
  result.checks.push_back(Check{"rollback", true, recovery.detail});
  result.checks.push_back(Check{"verify", true, restored.detail});

  if (!audit_ok || !journal_.append(completed, &error)) {
    degraded_ = true;
    if (active_ && active_->action_id == apply_action_id) active_.reset();
    result.status = Status::kDegraded;
    result.rollback.status = "AUDIT_PENDING";
    result.error = Error{ErrorCode::kInternal,
                         "effect restored but rollback audit is pending", error};
    const Json pending = result.to_json();
    journal_.append_result(envelope.idempotency_key, request_hash, pending, &error);
    return pending;
  }

  if (!ownership_.commit_generation(new_generation, &error)) {
    degraded_ = true;
    if (active_ && active_->action_id == apply_action_id) active_.reset();
    ActionResult degraded = result;
    degraded.status = Status::kDegraded;
    degraded.rollback.status = "EFFECT_RESTORED";
    degraded.error = Error{ErrorCode::kInternal,
                           "rollback committed but state.json persist failed", error};
    const Json degraded_response = degraded.to_json();
    journal_.append_result(envelope.idempotency_key, request_hash, degraded_response, &error);
    return degraded_response;
  }
  if (active_ && active_->action_id == apply_action_id) active_.reset();
  const Json response = result.to_json();
  journal_.append_result(envelope.idempotency_key, request_hash, response, &error);
  return response;
}

void Executor::monitor_loop() {
  while (running_) {
    for (int i = 0; i < 10 && running_; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!running_) break;
    std::lock_guard<std::mutex> lock(mu_);
    const std::int64_t now = now_epoch_ms();
    if (now - last_feature_ms_ >= 1000) {
      features_.sample();
      last_feature_ms_ = now;
    }
    // The autopilot is an internal caller of the same apply path a client uses,
    // so it runs on the monitor thread and never becomes a second writer.
    if (options_.auto_apply && now - auto_last_check_ms_ >= 2000) {
      auto_last_check_ms_ = now;
      run_auto_apply_locked(now);
    }
    if (!active_) {
      liveness_strikes_ = 0;
      continue;
    }
    if (now < next_fallback_attempt_ms_) continue;
    // A verified apply can still lose the resource afterwards, so presence is
    // re-checked while the effect is active. Three consecutive misses (~3s) are
    // required because a single read can land inside a kernel re-enable
    // transition, and a false positive would tear down a healthy scheduler.
    if (now - last_liveness_ms_ >= 1000) {
      last_liveness_ms_ = now;
      Applied probe;
      probe.data = active_->applied;
      probe.bindings = active_->bindings;
      std::string detail;
      if (skill_->liveness(probe, &detail)) {
        liveness_strikes_ = 0;
      } else if (++liveness_strikes_ >= 3) {
        liveness_strikes_ = 0;
        auto_fallback_locked("scheduler lost: " + detail);
        continue;
      } else {
        std::cerr << "[executor] liveness miss " << liveness_strikes_ << ": " << detail
                  << "\n";
      }
    }
    if (active_->ttl_ms > 0 && now - active_->applied_at_ms >= active_->ttl_ms) {
      auto_fallback_locked("policy TTL expired");
      continue;
    }
    const LeaseState lease = ownership_.lease();
    if (lease.expires_ms == 0 || now < lease.expires_ms) {
      if (now - last_refresh_ms_ >= 2000) {
        std::int64_t refresh_deadline = now + 500;
        if (active_->ttl_ms > 0) {
          const std::int64_t ttl_deadline = active_->applied_at_ms + active_->ttl_ms;
          if (ttl_deadline < refresh_deadline) refresh_deadline = ttl_deadline;
        }
        if (lease.expires_ms < refresh_deadline) refresh_deadline = lease.expires_ms;
        if (refresh_deadline > now + 50) {
          Applied applied;
          applied.data = active_->applied;
          applied.bindings = active_->bindings;
          RefreshContext context{refresh_deadline};
          std::string refresh_error;
          if (!skill_->refresh(applied, applied.bindings, context, &refresh_error)) {
            std::cerr << "[executor] refresh stopped: " << refresh_error << "\n";
          }
        }
        last_refresh_ms_ = now;
      }
      continue;
    }
    auto_fallback_locked("lease expired");
  }
}

void Executor::auto_fallback_locked(const std::string& reason) {
  if (!active_) return;
  const std::int64_t now = now_epoch_ms();
  if (now < next_fallback_attempt_ms_) return;
  const ActiveAction active = *active_;
  const std::string txn = "auto-" + random_hex(12);
  const std::string action_id = "rbk-" + random_hex(12);
  const Json rollback_of{{"original_transaction_id", active.txn},
                         {"apply_action_id", active.action_id},
                         {"snapshot_id", active.snapshot_id}};
  Snapshot snapshot;
  snapshot.id = active.snapshot_id;
  snapshot.data = active.snapshot;
  Applied applied;
  applied.data = active.applied;
  JournalRecord step;
  step.txn = txn;
  step.action_id = action_id;
  step.type = "ROLLBACK_PREPARED";
  step.generation = ownership_.generation();
  step.owner = active.owner;
  step.fence = active.fence;
  step.data = Json{{"rollback_of", rollback_of},
                   {"policy_sha256", active.policy_sha256},
                   {"snapshot", active.snapshot},
                   {"applied", active.applied},
                   {"reason", reason},
                   {"generation_before", ownership_.generation()}};
  std::string error;
  const bool audit_ok = journal_.append(step, &error);
  if (!audit_ok) {
    degraded_ = true;
    std::cerr << "[executor] auto fallback journal failed: " << error << "\n";
  }
  const RecoveryResult recovery = skill_->rollback(applied, snapshot, &error);
  if (!recovery.ok) {
    degraded_ = true;
    last_fallback_ = reason + ": rollback failed: " + recovery.detail;
    next_fallback_attempt_ms_ = now + 2000;
    if (audit_ok) {
      JournalRecord failed = step;
      failed.type = "RECOVERY_REQUIRED";
      failed.data = Json{{"detail", recovery.detail},
                         {"conflict", recovery.conflict},
                         {"reason", reason}};
      journal_.append(failed, &error);
    }
    return;
  }
  const VerifyResult restored = skill_->verify_rolled_back(snapshot, now + 5000);
  if (!restored.ok) {
    degraded_ = true;
    last_fallback_ = reason + ": verify failed: " + restored.detail;
    next_fallback_attempt_ms_ = now + 2000;
    if (audit_ok) {
      JournalRecord failed = step;
      failed.type = "RECOVERY_REQUIRED";
      failed.data = Json{{"detail", restored.detail},
                         {"conflict", true},
                         {"reason", reason}};
      journal_.append(failed, &error);
    }
    return;
  }

  // The effect is verifiably gone; release it even if the audit path failed.
  if (active_ && active_->action_id == active.action_id) active_.reset();
  next_fallback_attempt_ms_ = 0;

  if (!audit_ok) {
    degraded_ = true;
    last_fallback_ = reason + ": effect restored; audit pending: " + error;
    return;
  }

  const std::uint64_t new_generation = ownership_.generation() + 1;
  JournalRecord completed = step;
  completed.type = "ROLLED_BACK";
  completed.generation = new_generation;
  completed.data = Json{{"rollback_of", rollback_of},
                        {"snapshot_id", active.snapshot_id},
                        {"generation_advanced", true},
                        {"detail", recovery.detail},
                        {"reason", reason}};
  if (!journal_.append(completed, &error)) {
    degraded_ = true;
    last_fallback_ = reason + ": effect restored; audit pending: " + error;
    return;
  }
  if (!ownership_.commit_generation(new_generation, &error)) {
    degraded_ = true;
    last_fallback_ = reason + ": effect restored; generation persist failed: " + error;
    return;
  }
  degraded_ = true;
  last_fallback_ = reason;
  std::cerr << "[executor] auto fallback: " << reason << "\n";
}

void Executor::run_auto_apply_locked(std::int64_t now_ms) {
  if (options_.auto_owner.empty() || !options_.auto_baseline_policy.is_object() ||
      !options_.auto_baseline_policy.contains("spec")) {
    auto_last_hold_ = "autopilot baseline policy unavailable";
    return;
  }

  LeaseState lease = ownership_.lease();
  const bool mine = lease.owner == options_.auto_owner;
  const bool valid = lease.expires_ms > now_ms;
  if (valid && !mine) {
    // A live lease held by somebody else always outranks the autopilot.
    auto_last_hold_ = "lease held by " + lease.owner;
    return;
  }
  const bool renewing = mine && valid &&
                        lease.expires_ms - now_ms < options_.auto_lease_ttl_ms / 2;
  if (!mine || !valid || renewing) {
    std::string lease_error;
    LeaseState acquired;
    if (!ownership_.acquire_lease(options_.auto_owner,
                                  static_cast<std::uint64_t>(options_.auto_lease_ttl_ms),
                                  &acquired, &lease_error)) {
      auto_last_hold_ = "lease acquire failed: " + lease_error;
      return;
    }
    lease = acquired;
    std::cerr << "[executor] autopilot " << (renewing ? "renewed" : "took")
              << " lease fence=" << lease.fence << "\n";
  }

  const Json routing = router_.decide(features_.snapshot(), now_ms);
  const std::string routed = routing.value("expert", "");
  const Json gate = SloGate{}.evaluate(service_slo_input(routed, now_ms));

  // Track how long the routing target has been unchanged: a route that keeps
  // flipping is evidence the workload straddles a threshold, not that the policy
  // should follow every crossing.
  if (routed != auto_desired_) {
    auto_desired_ = routed;
    auto_desired_since_ms_ = now_ms;
  }
  const std::int64_t budget_cutoff = now_ms - options_.auto_budget_window_ms;
  auto_switch_times_.erase(
      std::remove_if(auto_switch_times_.begin(), auto_switch_times_.end(),
                     [budget_cutoff](std::int64_t at) { return at <= budget_cutoff; }),
      auto_switch_times_.end());

  AutoInput input;
  input.enabled = true;
  input.auto_owner = options_.auto_owner;
  input.lease_owner = lease.owner;
  input.lease_expires_ms = lease.expires_ms;
  input.routing_degraded = routing.value("degraded", false);
  input.routed_expert = routed;
  input.slo_allowed = gate.value("allowed", false);
  input.slo_code = gate.value("code", "");
  input.active_expert = active_ ? active_->policy_expert : std::string();
  input.active_age_ms = active_ ? now_ms - active_->applied_at_ms : 0;
  input.desired_stable_ms = now_ms - auto_desired_since_ms_;
  input.switch_times_ms = auto_switch_times_;
  input.last_action_ms = auto_last_action_ms_;
  input.min_interval_ms = options_.auto_min_interval_ms;
  input.now_ms = now_ms;
  input.dwell_ms = options_.auto_dwell_ms;
  input.candidate_hold_ms = options_.auto_candidate_hold_ms;
  input.budget_window_ms = options_.auto_budget_window_ms;
  input.budget_max_switches = options_.auto_budget_max_switches;
  input.measured_downtime_ms = options_.auto_measured_downtime_ms;

  const AutoDecision decision = auto_pilot_decide(input);
  if (!decision.act) {
    auto_last_hold_ = decision.hold;
    return;
  }

  // Switching means dropping the current effect and attaching the new one:
  // skill preflight refuses a second attach while an effect is in place, and
  // overwriting active_ without rolling it back would lose its ownership.
  // Rollback first, so the worst outcome of a failed switch is the safe state.
  // Rollback clears active_, so the expert being left has to be remembered here
  // or the post-switch measurement would have nothing to be judged against.
  const std::string from_expert = active_ ? active_->policy_expert : std::string();
  if (active_) {
    const Json rollback_of{{"apply_action_id", active_->action_id},
                           {"original_transaction_id", active_->txn},
                           {"snapshot_id", active_->snapshot_id}};
    const Json rollback_envelope{
        {"apiVersion", kApiVersion},
        {"request_id", "req-auto-rb-" + random_hex(12)},
        {"transaction_id", "txn-auto-rb-" + random_hex(12)},
        {"idempotency_key", "auto-rb-" + std::to_string(now_ms)},
        {"operation", "policy.rollback"},
        {"policy_sha256", active_->policy_sha256},
        {"lease_owner", lease.owner},
        {"fence_epoch", lease.fence},
        {"expected_generation", ownership_.generation()},
        {"issued_at", now_iso8601()},
        {"deadline", iso8601_from_ms(now_ms + 60000)},
        {"rollback_of", rollback_of}};
    const Json rolled = handle_rollback(Json{{"operation", "policy.rollback"},
                                             {"envelope", rollback_envelope}});
    const std::string rolled_status = rolled.value("status", "");
    if (rolled_status != "ROLLED_BACK") {
      auto_last_action_ms_ = now_ms;
      // The rollback already detached the effect, so this attempt paid a gap even
      // though it never re-attached: it has to consume budget.
      auto_switch_times_.push_back(now_ms);
      auto_last_hold_ = "switch aborted, rollback returned " + rolled_status;
      std::cerr << "[executor] autopilot " << auto_last_hold_ << "; staying on fair\n";
      return;
    }
  }

  Json policy = options_.auto_baseline_policy;
  policy["spec"]["expert"] = decision.expert;
  policy["spec"]["source"] = "rule-template";
  const std::string digest = sha256_hex(canonical_json(policy));
  const Json envelope{
      {"apiVersion", kApiVersion},
      {"request_id", "req-auto-ap-" + random_hex(12)},
      {"transaction_id", "txn-auto-ap-" + random_hex(12)},
      {"idempotency_key", "auto-ap-" + std::to_string(now_ms)},
      {"operation", "policy.apply"},
      {"policy_sha256", digest},
      {"lease_owner", lease.owner},
      {"fence_epoch", lease.fence},
      {"expected_generation", ownership_.generation()},
      {"issued_at", now_iso8601()},
      {"deadline", iso8601_from_ms(now_ms + 60000)}};
  const Json response = handle_apply(Json{{"operation", "policy.apply"},
                                          {"envelope", envelope},
                                          {"policy", policy}});
  auto_last_action_ms_ = now_ms;
  auto_last_expert_ = decision.expert;
  const std::string status = response.value("status", "");
  if (status == "VERIFIED") {
    auto_last_hold_.clear();
    auto_last_reason_ = decision.reason;
    auto_switch_times_.push_back(now_ms);
    if (!from_expert.empty() && from_expert != decision.expert) {
      outcome_from_ = from_expert;
      outcome_to_ = decision.expert;
      outcome_before_ = observation_;
      outcome_pending_ = has_observation_;
      outcome_windows_ = 0;
      if (!has_observation_) {
        last_outcome_ = "no service measurement before " + from_expert + " -> " +
                        decision.expert + "; outcome not evaluated";
      }
    }
    std::cerr << "[executor] autopilot switched " << decision.reason
              << "; switch " << auto_switch_times_.size() << "/"
              << options_.auto_budget_max_switches << " in "
              << options_.auto_budget_window_ms << "ms window, projected unprotected "
              << auto_switch_times_.size() * options_.auto_measured_downtime_ms << "ms\n";
    return;
  }
  auto_last_hold_ = "apply returned " + status;
  // Rollback succeeded but the apply did not: the gap was still paid, so it
  // counts against the budget rather than being treated as a free retry.
  auto_switch_times_.push_back(now_ms);
  std::cerr << "[executor] autopilot apply not verified: " << status << " "
            << response.value("error", Json(Json::object_t())).dump()
            << "; effect left rolled back\n";
}

bool Executor::reconcile_all(std::vector<std::string>* unresolved, std::string* error) {
  unresolved->clear();
  const std::vector<std::string> transactions = journal_.transactions_needing_reconcile();
  for (const auto& txn : transactions) {
    const std::vector<JournalRecord> records = journal_.transaction_records(txn);
    if (records.empty()) continue;
    const JournalRecord& last = records.back();
    const JournalRecord* prepared = nullptr;
    for (auto it = records.rbegin(); it != records.rend(); ++it) {
      if (it->type == "PREPARED" || it->type == "ROLLBACK_PREPARED") {
        prepared = &*it;
        break;
      }
    }
    if (prepared == nullptr) {
      unresolved->push_back(txn);
      continue;
    }
    ReconcileContext context;
    context.txn = txn;
    context.action_id = last.action_id;
    context.last_type = last.type;
    context.prepared = prepared->data;
    std::string reconcile_error;
    const RecoveryResult recovery = skill_->reconcile(context, &reconcile_error);
    if (recovery.ok) {
      JournalRecord resolved;
      resolved.txn = txn;
      resolved.action_id = last.action_id;
      resolved.type = "ROLLED_BACK";
      resolved.generation = ownership_.generation();
      resolved.data = Json{{"reconciled", true},
                           {"detail", recovery.detail},
                           {"generation_advanced", false}};
      if (!journal_.append(resolved, error)) return false;
      std::cerr << "[executor] reconciled " << txn << ": " << recovery.detail << "\n";
    } else {
      if (last.type != "RECOVERY_REQUIRED") {
        JournalRecord stuck;
        stuck.txn = txn;
        stuck.action_id = last.action_id;
        stuck.type = "RECOVERY_REQUIRED";
        stuck.generation = ownership_.generation();
        stuck.data = Json{{"detail", reconcile_error}, {"conflict", recovery.conflict}};
        if (!journal_.append(stuck, error)) return false;
      }
      unresolved->push_back(txn);
    }
  }
  degraded_ = !unresolved->empty();
  return true;
}

Json Executor::handle_reconcile(const Json& request) {
  const std::string request_id = request.value("request_id", "");
  std::string orphan_error;
  const RecoveryResult orphan = skill_->recover_orphan(&orphan_error);
  std::vector<std::string> unresolved;
  std::string error;
  if (!reconcile_all(&unresolved, &error)) {
    return rejection(request_id, ErrorCode::kInternal, "reconcile failed", error);
  }
  degraded_ = !unresolved.empty() || !orphan.ok;
  Json array = Json::array();
  for (const auto& txn : unresolved) array.push_back(txn);
  return Json{{"request_id", request_id},
              {"status", degraded_ ? "RECOVERY_REQUIRED" : "OK"},
              {"degraded", degraded_},
              {"orphan_recovery", Json{{"ok", orphan.ok}, {"detail", orphan.detail}}},
              {"unresolved", array},
              {"observed_generation", ownership_.generation()},
              {"journal_records", journal_.size()}};
}

}  // namespace ep
