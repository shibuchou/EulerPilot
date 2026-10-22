#include "ep/executor.hpp"

#include "ep/policy.hpp"
#include "ep/target.hpp"

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
    std::cerr << "[executor] orphan recovery failed: " << orphan_error << "\n";
  }
  std::vector<std::string> unresolved;
  if (!reconcile_all(&unresolved, error)) return false;
  degraded_ = !unresolved.empty() || !orphan.ok;
  for (const auto& txn : unresolved) {
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
  return Json{{"request_id", request_id},
              {"status", "OK"},
              {"instance_id", ownership_.instance_id()},
              {"skill", skill_->name()},
              {"generation", ownership_.generation()},
              {"degraded", degraded_},
              {"lease", ownership_.lease().to_json()},
              {"journal_records", journal_.size()},
              {"last_fallback", last_fallback_},
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

Json Executor::handle_slo(const Json& request) {
  const std::string expert = request.value("expert", "");
  const std::int64_t max_age = request.value("max_observation_age_ms", 1000LL);
  Json decision = SloGate{}.evaluate(features_.snapshot(), expert, max_age);
  decision["request_id"] = request.value("request_id", "");
  return decision;
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
  if (!ownership_.acquire_lease(owner, ttl_ms, &lease, &error)) {
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
                         "run transaction.reconcile"};
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
    const Json gate = SloGate{}.evaluate(features_.snapshot(), policy_summary.expert, 1000);
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
  active_ = ActiveAction{envelope.transaction_id, action_id, envelope.lease_owner,
                         envelope.fence_epoch, applied.data, snapshot.data,
                         snapshot.id, digest, now_epoch_ms(),
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
    if (!active_) continue;
    if (now < next_fallback_attempt_ms_) continue;
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
