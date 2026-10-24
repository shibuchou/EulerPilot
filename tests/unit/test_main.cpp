#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "ep/autopilot.hpp"
#include "ep/executor.hpp"
#include "ep/features.hpp"
#include "ep/journal.hpp"
#include "ep/mock_skill.hpp"
#include "ep/ownership.hpp"
#include "ep/policy.hpp"
#include "ep/router.hpp"
#include "ep/sched_skill.hpp"
#include "ep/slo.hpp"
#include "ep/target.hpp"
#include "ep/unsupported_skill.hpp"
#include "ep/util.hpp"

namespace {

class SlowMockSkill : public ep::MockSkill {
 public:
  using MockSkill::MockSkill;
  bool apply(const ep::Plan& plan, const ep::Snapshot& snapshot,
             const ep::LeaseState& lease, ep::Applied* out, std::string* error) override {
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    return ep::MockSkill::apply(plan, snapshot, lease, out, error);
  }
};

int g_failures = 0;

void check(bool condition, const char* expression, const char* file, int line) {
  if (condition) return;
  std::cerr << file << ":" << line << " CHECK failed: " << expression << "\n";
  ++g_failures;
}

#define CHECK(condition) check((condition), #condition, __FILE__, __LINE__)

std::filesystem::path make_temp_dir(const std::string& tag) {
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / ("ep-test-" + tag + "-" + ep::random_hex(6));
  std::filesystem::create_directories(dir);
  return dir;
}

ep::Json make_envelope(const std::string& operation, const ep::Json& payload,
                       const std::string& owner, std::uint64_t fence,
                       std::uint64_t generation, const std::string& idempotency_key,
                       const ep::Json& rollback_of = ep::Json::object()) {
  const std::string digest = ep::sha256_hex(ep::canonical_json(payload));
  ep::Json envelope{{"apiVersion", ep::kApiVersion},
                    {"request_id", "req-" + ep::random_hex(8)},
                    {"transaction_id", "txn-" + ep::random_hex(8)},
                    {"idempotency_key", idempotency_key},
                    {"operation", operation},
                    {"policy_sha256", digest},
                    {"lease_owner", owner},
                    {"fence_epoch", fence},
                    {"expected_generation", generation},
                    {"issued_at", ep::now_iso8601()},
                    {"deadline", ep::iso8601_from_ms(ep::now_epoch_ms() + 60000)}};
  if (!rollback_of.empty()) envelope["rollback_of"] = rollback_of;
  return envelope;
}

int read_mock_value(const std::filesystem::path& dir, bool* exists) {
  const std::filesystem::path file = dir / "mock" / "value.json";
  *exists = std::filesystem::exists(file);
  if (!*exists) return 0;
  std::ifstream input(file);
  std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  return ep::Json::parse(text).at("value").get<int>();
}

void test_sha256() {
  CHECK(ep::sha256_hex("") ==
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK(ep::sha256_hex("abc") ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

void test_canonical_json() {
  const ep::Json value = {{"b", 1}, {"a", {{"d", 2}, {"c", 3}}}};
  CHECK(ep::canonical_json(value) == "{\"a\":{\"c\":3,\"d\":2},\"b\":1}");
}

void test_iso8601() {
  std::int64_t offset = 0;
  std::int64_t utc = 0;
  CHECK(ep::parse_iso8601_ms("2026-10-21T10:00:00+08:00", &offset));
  CHECK(ep::parse_iso8601_ms("2026-10-21T02:00:00Z", &utc));
  CHECK(offset == utc);
  CHECK(!ep::parse_iso8601_ms("2026-10-21 02:00:00", &utc));
}

void test_journal() {
  const auto dir = make_temp_dir("journal");
  {
    ep::Journal journal(dir);
    std::string error;
    CHECK(journal.open(&error));
    ep::JournalRecord prepared;
    prepared.txn = "txn-journal-01";
    prepared.action_id = "act-journal-01";
    prepared.type = "PREPARED";
    prepared.generation = 0;
    CHECK(journal.append(prepared, &error));
    ep::JournalRecord committed = prepared;
    committed.type = "COMMITTED";
    committed.generation = 1;
    CHECK(journal.append(committed, &error));
    CHECK(journal.max_committed_generation() == 1);
    CHECK(journal.incomplete_transactions().empty());
  }
  {
    ep::Journal journal(dir);
    std::string error;
    CHECK(journal.open(&error));
    CHECK(journal.records().size() == 2);
    // Simulate a torn trailing record.
    const int fd = ::open((dir / "journal.jsonl").c_str(), O_WRONLY | O_APPEND);
    CHECK(fd >= 0);
    const std::string torn = "{\"seq\":3,\"type\":\"PRE";
    CHECK(::write(fd, torn.data(), torn.size()) == static_cast<ssize_t>(torn.size()));
    ::close(fd);
  }
  {
    ep::Journal journal(dir);
    std::string error;
    CHECK(journal.open(&error));
    CHECK(journal.records().size() == 2);
  }
  std::filesystem::remove_all(dir);
}

void test_ownership() {
  const auto dir = make_temp_dir("ownership");
  {
    ep::Ownership first(dir);
    std::string error;
    CHECK(first.open(&error));
    ep::Ownership second(dir);
    CHECK(!second.open(&error));

    ep::LeaseState lease;
    CHECK(first.acquire_lease("unit-owner-0001", 5000, &lease, &error));
    CHECK(lease.fence == 1);
    CHECK(first.validate_lease("unit-owner-0001", 1, &error));
    CHECK(first.acquire_lease("unit-owner-0001", 5000, &lease, &error));
    CHECK(lease.fence == 2);
    CHECK(!first.validate_lease("unit-owner-0001", 1, &error));
    CHECK(first.validate_lease("unit-owner-0001", 2, &error));
  }
  {
    ep::Ownership reopened(dir);
    std::string error;
    CHECK(reopened.open(&error));
    CHECK(reopened.lease().fence == 2);
  }
  std::filesystem::remove_all(dir);
}

void test_executor_apply_and_rollback() {
  const auto dir = make_temp_dir("executor");
  {
    ep::Executor executor(dir, std::make_unique<ep::MockSkill>(dir / "mock"),
                          ep::ExecutorOptions{true});
    std::string error;
    CHECK(executor.init(&error));

    ep::Json lease = executor.handle(ep::Json{{"operation", "lease.acquire"},
                                              {"request_id", "req-lease-01"},
                                              {"lease_owner", "unit-owner-01"}});
    CHECK(lease["status"] == "OK");
    const std::uint64_t fence = lease["fence_epoch"].get<std::uint64_t>();

    const ep::Json intent{{"value", 42}};
    ep::Json envelope = make_envelope("policy.apply", intent, "unit-owner-01", fence, 0,
                                      "idem-apply-0001");
    const ep::Json request{{"operation", "policy.apply"},
                           {"envelope", envelope},
                           {"intent", intent}};
    ep::Json response = executor.handle(request);
    CHECK(response["status"] == "VERIFIED");
    CHECK(response["observed_generation"] == 1);
    CHECK(!response["action_id"].get<std::string>().empty());
    bool exists = false;
    CHECK(read_mock_value(dir, &exists) == 42);
    CHECK(exists);

    // Same idempotency key and payload returns the stored result.
    const ep::Json replay = executor.handle(request);
    CHECK(replay["status"] == "VERIFIED");
    CHECK(replay["observed_generation"] == 1);
    CHECK(executor.generation() == 1);

    // Same key with different content is rejected.
    const ep::Json other{{"value", 43}};
    const ep::Json other_envelope = make_envelope("policy.apply", other, "unit-owner-01",
                                                  fence, 1, "idem-apply-0001");
    const ep::Json conflict = executor.handle(ep::Json{{"operation", "policy.apply"},
                                                       {"envelope", other_envelope},
                                                       {"intent", other}});
    CHECK(conflict["status"] == "REJECTED");
    CHECK(conflict["error"]["code"] == "IDEMPOTENCY_CONFLICT");

    // Stale fence is rejected.
    ep::Json second_lease = executor.handle(ep::Json{{"operation", "lease.acquire"},
                                                     {"request_id", "req-lease-02"},
                                                     {"lease_owner", "unit-owner-01"}});
    const std::uint64_t second_fence = second_lease["fence_epoch"].get<std::uint64_t>();
    const ep::Json stale_envelope = make_envelope("policy.apply", other, "unit-owner-01",
                                                  fence, 1, "idem-apply-0002");
    const ep::Json stale = executor.handle(ep::Json{{"operation", "policy.apply"},
                                                    {"envelope", stale_envelope},
                                                    {"intent", other}});
    CHECK(stale["status"] == "REJECTED");
    CHECK(stale["error"]["code"] == "OWNERSHIP_CONFLICT");

    // Wrong expected generation is rejected.
    const ep::Json old_generation = make_envelope("policy.apply", other, "unit-owner-01",
                                                  second_fence, 0, "idem-apply-0003");
    const ep::Json generation = executor.handle(ep::Json{{"operation", "policy.apply"},
                                                         {"envelope", old_generation},
                                                         {"intent", other}});
    CHECK(generation["status"] == "REJECTED");
    CHECK(generation["error"]["code"] == "GENERATION_CONFLICT");

    // Apply a second value, then roll it back to the first one.
    const ep::Json second_envelope = make_envelope("policy.apply", other, "unit-owner-01",
                                                   second_fence, 1, "idem-apply-0004");
    ep::Json second = executor.handle(ep::Json{{"operation", "policy.apply"},
                                               {"envelope", second_envelope},
                                               {"intent", other}});
    CHECK(second["status"] == "VERIFIED");
    CHECK(second["observed_generation"] == 2);
    CHECK(read_mock_value(dir, &exists) == 43);

    const ep::Json rollback_of{
        {"apply_action_id", second["action_id"]},
        {"original_transaction_id", second_envelope["transaction_id"]},
        {"snapshot_id", second["snapshot_id"]}};
    const ep::Json rollback_envelope =
        make_envelope("policy.rollback", other, "unit-owner-01", second_fence, 2,
                      "idem-rollback-01", rollback_of);
    const ep::Json rolled = executor.handle(ep::Json{{"operation", "policy.rollback"},
                                                     {"envelope", rollback_envelope}});
    CHECK(rolled["status"] == "ROLLED_BACK");
    CHECK(rolled["observed_generation"] == 3);
    CHECK(read_mock_value(dir, &exists) == 42);
  }
  std::filesystem::remove_all(dir);
}

void test_autopilot_rules() {
  ep::AutoInput base;
  base.enabled = true;
  base.auto_owner = "autopilot-v2";
  base.lease_owner = "autopilot-v2";
  base.lease_expires_ms = 60000;
  base.routed_expert = "latency-balanced";
  base.slo_allowed = true;
  base.active_expert = "safe-basic";
  base.min_interval_ms = 15000;
  base.now_ms = 50000;
  // Pass the suppression gates by default so each case below isolates one of them.
  base.active_age_ms = 120000;
  base.desired_stable_ms = 20000;

  const auto go = ep::auto_pilot_decide(base);
  CHECK(go.act);
  CHECK(go.expert == "latency-balanced");

  ep::AutoInput off = base;
  off.enabled = false;
  CHECK(ep::auto_pilot_decide(off).hold == "disabled");

  ep::AutoInput anonymous = base;
  anonymous.auto_owner = "";
  CHECK(ep::auto_pilot_decide(anonymous).hold == "no auto owner configured");

  // A live operator lease always outranks the autopilot.
  ep::AutoInput human = base;
  human.lease_owner = "cli-operator-1";
  CHECK(ep::auto_pilot_decide(human).hold == "lease held by cli-operator-1");

  ep::AutoInput expired = base;
  expired.lease_expires_ms = 40000;
  CHECK(ep::auto_pilot_decide(expired).hold == "lease expired");

  // It switches an effect that a human already applied; it never introduces one.
  ep::AutoInput noactive = base;
  noactive.active_expert = "";
  CHECK(ep::auto_pilot_decide(noactive).hold == "no active policy to switch");

  ep::AutoInput degraded = base;
  degraded.routing_degraded = true;
  CHECK(ep::auto_pilot_decide(degraded).hold == "routing degraded");

  ep::AutoInput denied = base;
  denied.slo_allowed = false;
  denied.slo_code = "PSI_MISSING";
  CHECK(ep::auto_pilot_decide(denied).hold == "slo denied: PSI_MISSING");

  ep::AutoInput same = base;
  same.active_expert = "latency-balanced";
  CHECK(ep::auto_pilot_decide(same).hold == "already in place: latency-balanced");

  // Narrower than the policy schema enum: cache-local was never exercised on SP3.
  ep::AutoInput beyond = base;
  beyond.routed_expert = "cache-local";
  CHECK(ep::auto_pilot_decide(beyond).hold == "expert outside autopilot whitelist: cache-local");
  CHECK(!ep::auto_pilot_expert_allowed("cache-local"));
  CHECK(ep::auto_pilot_expert_allowed("throughput-balanced"));

  ep::AutoInput cooling = base;
  cooling.last_action_ms = 45000;
  CHECK(ep::auto_pilot_decide(cooling).hold.rfind("cooldown", 0) == 0);
  ep::AutoInput cooling_outside = cooling;
  cooling_outside.last_action_ms = 30000;
  CHECK(ep::auto_pilot_decide(cooling_outside).act);

  // Suppression, sized from the measured 0.3-0.7s cost of one switch.
  ep::AutoInput fresh = base;
  fresh.active_age_ms = 1000;
  CHECK(ep::auto_pilot_decide(fresh).hold.rfind("dwell 60000ms not met", 0) == 0);

  ep::AutoInput flapping = base;
  flapping.desired_stable_ms = 2000;
  CHECK(ep::auto_pilot_decide(flapping).hold ==
        "routing target stable 2000ms of 15000ms required");

  ep::AutoInput spent = base;
  spent.switch_times_ms = {45000, 40000};
  CHECK(ep::auto_pilot_decide(spent).hold ==
        "switch budget 2/2 in 600000ms window");

  // Older switches fall out of the rolling window and the budget frees up.
  ep::AutoInput aged = base;
  aged.switch_times_ms = {40000, -700000};
  CHECK(ep::auto_pilot_decide(aged).act);

  // Authority outranks suppression: an operator lease is reported first.
  ep::AutoInput operator_first = base;
  operator_first.lease_owner = "cli-operator";
  operator_first.active_age_ms = 0;
  operator_first.switch_times_ms = {49000, 48000};
  CHECK(ep::auto_pilot_decide(operator_first).hold == "lease held by cli-operator");
}

void test_executor_external_change() {
  const auto dir = make_temp_dir("external");
  {
    ep::Executor executor(dir, std::make_unique<ep::MockSkill>(dir / "mock"),
                          ep::ExecutorOptions{true});
    std::string error;
    CHECK(executor.init(&error));
    ep::Json lease = executor.handle(ep::Json{{"operation", "lease.acquire"},
                                              {"request_id", "req-lease-01"},
                                              {"lease_owner", "unit-owner-01"}});
    const std::uint64_t fence = lease["fence_epoch"].get<std::uint64_t>();

    const ep::Json intent{{"value", 9}};
    const ep::Json envelope = make_envelope("policy.apply", intent, "unit-owner-01", fence, 0,
                                            "idem-ext-0001");
    ep::Json applied = executor.handle(ep::Json{{"operation", "policy.apply"},
                                                {"envelope", envelope},
                                                {"intent", intent}});
    CHECK(applied["status"] == "VERIFIED");

    // Someone else changes the state after the apply.
    std::ofstream(dir / "mock" / "value.json") << "{\"value\":123}\n";

    const ep::Json rollback_of{
        {"apply_action_id", applied["action_id"]},
        {"original_transaction_id", envelope["transaction_id"]},
        {"snapshot_id", applied["snapshot_id"]}};
    const ep::Json rollback_envelope =
        make_envelope("policy.rollback", intent, "unit-owner-01", fence, 1,
                      "idem-ext-rb-01", rollback_of);
    const ep::Json rolled = executor.handle(ep::Json{{"operation", "policy.rollback"},
                                                     {"envelope", rollback_envelope}});
    CHECK(rolled["status"] == "RECOVERY_REQUIRED");
    CHECK(rolled["rollback"]["status"] == "FAILED");
    CHECK(executor.degraded());
  }
  std::filesystem::remove_all(dir);
}

void test_executor_reconcile_after_crash() {
  const auto dir = make_temp_dir("reconcile");
  {
    std::filesystem::create_directories(dir / "mock");
    std::ofstream(dir / "mock" / "value.json") << "{\"value\":42}\n";
    {
      ep::Journal journal(dir);
      std::string error;
      CHECK(journal.open(&error));
      ep::JournalRecord prepared;
      prepared.txn = "txn-crash-0001";
      prepared.action_id = "act-crash-0001";
      prepared.type = "PREPARED";
      prepared.generation = 0;
      prepared.data = ep::Json{{"intent", {{"value", 42}}},
                               {"snapshot", ep::Json{{"exists", false}}},
                               {"snapshot_id", "snap-crash-0001"},
                               {"policy_sha256", std::string(64, 'a')}};
      CHECK(journal.append(prepared, &error));
      prepared.type = "APPLYING";
      prepared.data = ep::Json::object();
      CHECK(journal.append(prepared, &error));
    }
    ep::Executor executor(dir, std::make_unique<ep::MockSkill>(dir / "mock"),
                          ep::ExecutorOptions{true});
    std::string error;
    CHECK(executor.init(&error));
    CHECK(!executor.degraded());
    bool exists = true;
    CHECK(read_mock_value(dir, &exists) == 0);
    CHECK(!exists);
    const ep::Json status = executor.handle(ep::Json{{"operation", "policy.status"},
                                                     {"request_id", "req-status-01"}});
    CHECK(status["status"] == "OK");
    CHECK(status["incomplete_transactions"].empty());
  }
  std::filesystem::remove_all(dir);
}

void test_unsupported_skill() {
  const auto dir = make_temp_dir("unsupported");
  {
    ep::Executor executor(dir, std::make_unique<ep::UnsupportedSkill>("sched"),
                          ep::ExecutorOptions{true});
    std::string error;
    CHECK(executor.init(&error));
    ep::Json lease = executor.handle(ep::Json{{"operation", "lease.acquire"},
                                              {"request_id", "req-lease-01"},
                                              {"lease_owner", "unit-owner-01"}});
    const std::uint64_t fence = lease["fence_epoch"].get<std::uint64_t>();
    const ep::Json intent{{"value", 1}};
    const ep::Json envelope = make_envelope("policy.apply", intent, "unit-owner-01", fence, 0,
                                            "idem-unsupported-01");
    const ep::Json response = executor.handle(ep::Json{{"operation", "policy.apply"},
                                                       {"envelope", envelope},
                                                       {"intent", intent}});
    CHECK(response["status"] == "REJECTED");
    CHECK(response["error"]["code"] == "UNSUPPORTED");
  }
  std::filesystem::remove_all(dir);
}

void write_text_file(const std::filesystem::path& path, const std::string& text) {
  std::ofstream output(path);
  output << text;
}

std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
  return text;
}

ep::Json valid_policy() {
  return ep::Json{
      {"apiVersion", ep::kApiVersion},
      {"kind", "SchedulingPolicy"},
      {"metadata", ep::Json{{"name", "unit-policy"}}},
      {"spec",
       ep::Json{
           {"scope", "scheduler-instance"},
           {"mode", "class"},
           {"expert", "latency-balanced"},
           {"source", "human"},
           {"targets",
            ep::Json::array({ep::Json{{"kind", "cgroup"},
                                      {"path", "/sys/fs/cgroup/system.slice"},
                                      {"role", "FOREGROUND"},
                                      {"role_source", "explicit"}}})},
           {"parameters", ep::Json{{"lat_slice_ns", 2000000},
                                   {"comp_slice_ns", 4000000},
                                   {"cache_slice_ns", 4000000},
                                   {"bg_slice_ns", 2000000},
                                   {"preempt_threshold_ns", 500000},
                                   {"bg_vtime_pct", 200},
                                   {"llc_affinity", false}}},
           {"safety", ep::Json{{"min_dwell_ms", 5000},
                               {"max_observation_age_ms", 1000},
                               {"heartbeat_period_ms", 250},
                               {"heartbeat_timeout_ms", 2000},
                               {"policy_ttl_ms", 60000},
                               {"quiet_max_regression_pct", 5},
                               {"evaluation_min_bg_progress_ratio", 0.5},
                               {"bg_work_unit", "requests"},
                               {"bg_progress_window_ms", 5000},
                               {"min_bg_work_units_per_sec", 1},
                               {"max_bg_wait_ms", 1000},
                               {"fallback", "verified-safe-profile"},
                               {"allow_affinity_change", false}}}}} };
}

void test_policy_validation() {
  ep::PolicySummary summary;
  std::string error;
  CHECK(ep::validate_scheduling_policy(valid_policy(), &summary, &error));
  CHECK(summary.name == "unit-policy");
  CHECK(summary.mode == "class");
  CHECK(summary.targets.size() == 1);

  auto expect_invalid = [](const ep::Json& policy, const char* label) {
    ep::PolicySummary local;
    std::string message;
    if (ep::validate_scheduling_policy(policy, &local, &message)) {
      std::cerr << "expected invalid policy: " << label << "\n";
      ++g_failures;
    } else if (message.empty()) {
      std::cerr << "invalid policy without an error message: " << label << "\n";
      ++g_failures;
    }
  };

  {
    // The online forward ceiling is optional: absent validates, in range validates,
    // out of range is refused rather than clamped.
    auto with_ceiling = valid_policy();
    with_ceiling["spec"]["safety"]["max_foreground_p99_ms"] = 5000;
    ep::PolicySummary ceiling_summary;
    std::string ceiling_error;
    CHECK(ep::validate_scheduling_policy(with_ceiling, &ceiling_summary, &ceiling_error));
    CHECK(ceiling_summary.safety.value("max_foreground_p99_ms", 0LL) == 5000);

    auto zero = with_ceiling;
    zero["spec"]["safety"]["max_foreground_p99_ms"] = 0;
    expect_invalid(zero, "foreground ceiling of zero is not a limit");
    auto huge = with_ceiling;
    huge["spec"]["safety"]["max_foreground_p99_ms"] = 70000;
    expect_invalid(huge, "foreground ceiling beyond the declared range");
  }

  {
    auto policy = valid_policy();
    policy["spec"].erase("parameters");
    expect_invalid(policy, "missing parameters");
  }
  {
    auto policy = valid_policy();
    policy["spec"]["extra"] = 1;
    expect_invalid(policy, "unknown spec field");
  }
  {
    auto policy = valid_policy();
    policy["metadata"]["name"] = "Bad Name";
    expect_invalid(policy, "bad metadata name");
  }
  {
    auto policy = valid_policy();
    policy["spec"]["mode"] = "turbo";
    expect_invalid(policy, "invalid mode");
  }
  {
    auto policy = valid_policy();
    policy["spec"]["parameters"]["lat_slice_ns"] = 1;
    expect_invalid(policy, "slice out of range");
  }
  {
    auto policy = valid_policy();
    policy["spec"]["targets"].push_back(policy["spec"]["targets"][0]);
    expect_invalid(policy, "duplicate target");
  }
  {
    auto policy = valid_policy();
    policy["spec"]["targets"][0]["path"] = "/sys/fs/cgroup/../etc";
    expect_invalid(policy, "path traversal");
  }
  {
    auto policy = valid_policy();
    policy["spec"]["safety"]["heartbeat_timeout_ms"] = 250;
    expect_invalid(policy, "timeout not greater than period");
  }
  {
    auto policy = valid_policy();
    policy["spec"]["safety"]["allow_affinity_change"] = true;
    expect_invalid(policy, "affinity change allowed");
  }
  {
    auto policy = valid_policy();
    policy["spec"]["safety"]["policy_ttl_ms"] = 5000;
    expect_invalid(policy, "ttl not greater than dwell");
  }
  {
    auto policy = valid_policy();
    policy["spec"]["safety"]["max_observation_age_ms"] = 3000;
    expect_invalid(policy, "observation age exceeds heartbeat timeout");
  }
}

void test_target_resolution() {
  if (!std::filesystem::exists("/sys/fs/cgroup/cgroup.controllers")) {
    std::cout << "skip target resolution test (cgroup v2 not mounted)\n";
    return;
  }
  ep::PolicyTarget target{"cgroup", "/sys/fs/cgroup", "NEUTRAL", "explicit"};
  std::vector<ep::TargetBinding> bindings;
  std::string error;
  CHECK(ep::resolve_targets({target}, &bindings, &error));
  CHECK(bindings.size() == 1);
  CHECK(bindings[0].inode != 0);
  CHECK(!bindings[0].boot_id.empty());
  CHECK(bindings[0].cgroup_fd != nullptr);
  CHECK(*bindings[0].cgroup_fd >= 0);
  std::vector<std::int64_t> members;
  CHECK(ep::read_cgroup_members(bindings[0], &members, &error));
  if (bindings[0].cgroup_id == 0) {
    std::cout << "cgroup_id unavailable on this mount (non-root/no exportfs)\n";
  } else {
    std::cout << "cgroup_id=" << bindings[0].cgroup_id << "\n";
  }
  CHECK(ep::revalidate_targets(bindings, &error));

  ep::PolicyTarget pid_target{"pid", "", "FOREGROUND", "explicit"};
  pid_target.pid = static_cast<std::int64_t>(::getpid());
  CHECK(ep::resolve_targets({pid_target}, &bindings, &error));
  CHECK(bindings.size() == 1);
  CHECK(bindings[0].pid == static_cast<std::int64_t>(::getpid()));
  CHECK(bindings[0].starttime != 0);
  CHECK(bindings[0].pidfd != nullptr);
  CHECK(*bindings[0].pidfd >= 0);
  CHECK(ep::revalidate_targets(bindings, &error));

  const pid_t child = ::fork();
  if (child == 0) {
    ::pause();
    _exit(0);
  }
  if (child > 0) {
    ep::PolicyTarget child_target{"pid", "", "FOREGROUND", "explicit"};
    child_target.pid = static_cast<std::int64_t>(child);
    std::vector<ep::TargetBinding> child_bindings;
    CHECK(ep::resolve_targets({child_target}, &child_bindings, &error));
    CHECK(ep::revalidate_targets(child_bindings, &error));
    ::kill(child, SIGKILL);
    ::waitpid(child, nullptr, 0);
    CHECK(!ep::revalidate_targets(child_bindings, &error));
  }

  pid_target.pid = 4194304;
  CHECK(!ep::resolve_targets({pid_target}, &bindings, &error));

  target.path = "/sys/fs/cgroup/ep-test-missing-cgroup";
  CHECK(!ep::resolve_targets({target}, &bindings, &error));
}

void test_target_binding_lifetime() {
  const auto dir = make_temp_dir("target-lifetime");
  const auto cgroup = dir / "cg";
  std::filesystem::create_directories(cgroup);
  write_text_file(cgroup / "cgroup.controllers", "\n");
  write_text_file(cgroup / "cgroup.procs", "1\n");
  ep::PolicyTarget target{"cgroup", cgroup.string(), "BACKGROUND", "explicit"};
  std::vector<ep::TargetBinding> bindings;
  std::string error;
  CHECK(ep::resolve_targets({target}, &bindings, &error));
  CHECK(bindings.size() == 1);
  CHECK(bindings[0].cgroup_fd != nullptr);
  std::vector<std::int64_t> members;
  CHECK(ep::read_cgroup_members(bindings[0], &members, &error));
  CHECK(members.size() == 1 && members[0] == 1);
  CHECK(ep::revalidate_binding(bindings[0], &error));

  std::filesystem::rename(cgroup, dir / "cg-old");
  std::filesystem::create_directories(cgroup);
  write_text_file(cgroup / "cgroup.controllers", "\n");
  CHECK(!ep::revalidate_binding(bindings[0], &error));
  std::filesystem::remove_all(dir);
}

void test_refresh_revokes_stale_members() {
  const auto dir = make_temp_dir("refresh-revoke");
  const auto cgroup = dir / "cg";
  std::filesystem::create_directories(cgroup);
  write_text_file(cgroup / "cgroup.controllers", "\n");
  write_text_file(cgroup / "cgroup.procs", "111\n");
  ep::PolicyTarget target{"cgroup", cgroup.string(), "BACKGROUND", "explicit"};
  std::vector<ep::TargetBinding> bindings;
  std::string error;
  CHECK(ep::resolve_targets({target}, &bindings, &error));

  const auto log = dir / "ctrl.log";
  const auto ctrl = dir / "fake-ctrl.sh";
  write_text_file(ctrl, "#!/usr/bin/env bash\nprintf '%s\\n' \"$*\" >> \"" +
                            log.string() + "\"\necho '{\"ok\":true}'\n");
  CHECK(::chmod(ctrl.c_str(), 0755) == 0);

  ep::SchedPaths paths;
  paths.state_dir = dir / "state";
  paths.ctrl = ctrl.string();
  paths.ctrl_timeout_ms = 1000;
  ep::SchedSkill skill(paths);
  ep::Applied applied;
  applied.bindings = bindings;
  ep::RefreshContext context{ep::now_epoch_ms() + 1000};
  CHECK(skill.refresh(applied, applied.bindings, context, &error));
  std::string first = read_text_file(log);
  CHECK(first.find("class-set") != std::string::npos);
  CHECK(first.find("--pid 111") != std::string::npos);

  write_text_file(cgroup / "cgroup.procs", "222\n");
  CHECK(skill.refresh(applied, applied.bindings, context, &error));
  const std::string second = read_text_file(log);
  CHECK(second.find("class-del") != std::string::npos);
  CHECK(second.find("--pid 111") != std::string::npos);
  CHECK(second.find("--pid 222") != std::string::npos);
  std::filesystem::remove_all(dir);
}

void test_refresh_reclassifies_in_place() {
  const auto dir = make_temp_dir("refresh-reclass");
  const auto cgroup = dir / "cg";
  std::filesystem::create_directories(cgroup);
  write_text_file(cgroup / "cgroup.controllers", "\n");
  write_text_file(cgroup / "cgroup.procs", "333\n");
  ep::PolicyTarget target{"cgroup", cgroup.string(), "FOREGROUND", "explicit"};
  target.runtime_class = "LAT";
  std::vector<ep::TargetBinding> bindings;
  std::string error;
  CHECK(ep::resolve_targets({target}, &bindings, &error));

  const auto log = dir / "ctrl.log";
  const auto ctrl = dir / "fake-ctrl.sh";
  write_text_file(ctrl, "#!/usr/bin/env bash\nprintf '%s\\n' \"$*\" >> \"" + log.string() +
                            "\"\necho '{\"ok\":true}'\n");
  CHECK(::chmod(ctrl.c_str(), 0755) == 0);

  ep::SchedPaths paths;
  paths.state_dir = dir / "state";
  paths.ctrl = ctrl.string();
  paths.ctrl_timeout_ms = 1000;
  ep::SchedSkill skill(paths);
  ep::Applied applied;
  applied.bindings = bindings;
  ep::RefreshContext context{ep::now_epoch_ms() + 1000};
  CHECK(skill.refresh(applied, applied.bindings, context, &error));
  const std::string first = read_text_file(log);
  CHECK(first.find("--class LAT") != std::string::npos);
  CHECK(first.find("--pid 333") != std::string::npos);

  // Same pid, different declared class: it has to be re-asserted in place. Deleting
  // first would let the task run as NORMAL for a refresh period.
  std::vector<ep::TargetBinding> reclassed = bindings;
  reclassed[0].runtime_class = "CACHE";
  CHECK(skill.refresh(applied, reclassed, context, &error));
  std::string delta = read_text_file(log);
  delta = delta.substr(first.size());
  CHECK(delta.find("--class CACHE") != std::string::npos);
  CHECK(delta.find("--pid 333") != std::string::npos);
  CHECK(delta.find("class-del") == std::string::npos);

  // Declaring NORMAL means "no entry". ctrl_class_set treats NORMAL as a no-op by
  // design, so the old tier must be deleted explicitly or it stays in the kernel map.
  std::vector<ep::TargetBinding> plain = reclassed;
  plain[0].runtime_class = "NORMAL";
  plain[0].role = "NEUTRAL";
  CHECK(skill.refresh(applied, plain, context, &error));
  const std::string total = read_text_file(log);
  const std::string tail = total.substr(first.size() + delta.size());
  CHECK(tail.find("class-del") != std::string::npos);
  CHECK(tail.find("--pid 333") != std::string::npos);
  CHECK(tail.find("--class") == std::string::npos);
  std::filesystem::remove_all(dir);
}

void test_sched_skill_fake_loader() {
  const auto dir = make_temp_dir("schedskill");
  const auto sched_ext = dir / "sched_ext";
  const auto pin_dir = dir / "pin";
  const auto state_dir = dir / "state";
  const auto run_dir = dir / "exec";
  std::filesystem::create_directories(sched_ext / "root");
  std::filesystem::create_directories(pin_dir);
  std::filesystem::create_directories(state_dir);
  write_text_file(sched_ext / "state", "disabled\n");
  write_text_file(sched_ext / "root" / "ops", "\n");
  write_text_file(sched_ext / "enable_seq", "0\n");

  const auto loader = dir / "fake-loader.sh";
  write_text_file(loader, R"BASH(#!/usr/bin/env bash
set -e
se="${EP_FAKE_SCHED_EXT:?}"
pin="${EP_FAKE_PIN:?}"
mkdir -p "$se/root" "$pin"
: > "$pin/cfg"
: > "$pin/class_map"
: > "$pin/stats"
echo eulerpilot > "$se/root/ops"
echo $(( $(cat "$se/enable_seq") + 1 )) > "$se/enable_seq"
echo enabled > "$se/state"
trap 'echo disabled > "$se/state"; : > "$se/root/ops"; exit 0' TERM INT
while true; do sleep 0.05; done
)BASH");
  CHECK(::chmod(loader.c_str(), 0755) == 0);
  CHECK(::setenv("EP_FAKE_SCHED_EXT", sched_ext.c_str(), 1) == 0);
  CHECK(::setenv("EP_FAKE_PIN", pin_dir.c_str(), 1) == 0);

  ep::SchedPaths paths;
  paths.state_dir = state_dir;
  paths.loader = loader.string();
  paths.sched_ext_dir = sched_ext.string();
  paths.pin_dir = pin_dir.string();

  // The fake-loader test bypasses policy validation via test-mode intents;
  // policy validation itself is covered by test_policy_validation and the real
  // integration_sched test on SP3.
  const ep::Json intent{{"mode", "class"}};

  {
    ep::Executor executor(run_dir, std::make_unique<ep::SchedSkill>(paths),
                          ep::ExecutorOptions{true});
    std::string error;
    CHECK(executor.init(&error));
    ep::Json lease = executor.handle(ep::Json{{"operation", "lease.acquire"},
                                              {"request_id", "req-lease-s1"},
                                              {"lease_owner", "unit-owner-01"}});
    CHECK(lease["status"] == "OK");
    const std::uint64_t fence = lease["fence_epoch"].get<std::uint64_t>();
    const ep::Json envelope = make_envelope("policy.apply", intent, "unit-owner-01", fence, 0,
                                            "idem-sched-0001");
    ep::Json response = executor.handle(ep::Json{{"operation", "policy.apply"},
                                                  {"envelope", envelope},
                                                  {"intent", intent}});
    CHECK(response["status"] == "VERIFIED");
    CHECK(read_text_file(sched_ext / "state") == "enabled");
    CHECK(std::filesystem::exists(state_dir / "sched-runtime.json"));

    const ep::Json rollback_of{
        {"apply_action_id", response["action_id"]},
        {"original_transaction_id", envelope["transaction_id"]},
        {"snapshot_id", response["snapshot_id"]}};
    const ep::Json rollback_envelope =
        make_envelope("policy.rollback", intent, "unit-owner-01", fence, 1,
                      "idem-sched-rb01", rollback_of);
    const ep::Json rolled = executor.handle(ep::Json{{"operation", "policy.rollback"},
                                                     {"envelope", rollback_envelope}});
    CHECK(rolled["status"] == "ROLLED_BACK");
    CHECK(read_text_file(sched_ext / "state") == "disabled");
    CHECK(!std::filesystem::exists(state_dir / "sched-runtime.json"));
    CHECK(!std::filesystem::exists(pin_dir / "cfg"));
  }

  // Leave an orphan loader from a committed apply, then start a new executor.
  {
    ep::Executor executor(run_dir, std::make_unique<ep::SchedSkill>(paths),
                          ep::ExecutorOptions{true});
    std::string error;
    CHECK(executor.init(&error));
    ep::Json lease = executor.handle(ep::Json{{"operation", "lease.acquire"},
                                              {"request_id", "req-lease-s2"},
                                              {"lease_owner", "unit-owner-01"}});
    const std::uint64_t fence = lease["fence_epoch"].get<std::uint64_t>();
    const ep::Json envelope = make_envelope("policy.apply", intent, "unit-owner-01", fence, 2,
                                            "idem-sched-0002");
    ep::Json response = executor.handle(ep::Json{{"operation", "policy.apply"},
                                                  {"envelope", envelope},
                                                  {"intent", intent}});
    CHECK(response["status"] == "VERIFIED");
  }
  CHECK(read_text_file(sched_ext / "state") == "enabled");
  CHECK(std::filesystem::exists(state_dir / "sched-runtime.json"));

  {
    ep::Executor executor(run_dir, std::make_unique<ep::SchedSkill>(paths),
                          ep::ExecutorOptions{false});
    std::string error;
    CHECK(executor.init(&error));
    CHECK(!executor.degraded());
    CHECK(read_text_file(sched_ext / "state") == "disabled");
    CHECK(!std::filesystem::exists(state_dir / "sched-runtime.json"));
    CHECK(!std::filesystem::exists(pin_dir / "cfg"));
  }
  std::filesystem::remove_all(dir);
}

void test_sched_enable_seq_semantics() {
  const auto dir = make_temp_dir("sched-verify-seq");
  const auto sched_ext = dir / "sched_ext";
  const auto pin_dir = dir / "pin";
  const auto state_dir = dir / "state";
  const auto run_dir = dir / "exec";
  std::filesystem::create_directories(sched_ext / "root");
  std::filesystem::create_directories(pin_dir);
  std::filesystem::create_directories(state_dir);
  write_text_file(sched_ext / "state", "disabled\n");
  write_text_file(sched_ext / "root" / "ops", "\n");
  write_text_file(sched_ext / "enable_seq", "0\n");
  write_text_file(sched_ext / "enable_seq", "100\n");

  const auto loader = dir / "fake-loader-seq.sh";
  write_text_file(loader, R"BASH(#!/usr/bin/env bash
set -e
se="${EP_FAKE_SCHED_EXT:?}"
pin="${EP_FAKE_PIN:?}"
mkdir -p "$se/root" "$pin"
: > "$pin/cfg"
: > "$pin/class_map"
: > "$pin/stats"
echo eulerpilot > "$se/root/ops"
echo $(( $(cat "$se/enable_seq") + 1 )) > "$se/enable_seq"
echo enabled > "$se/state"
trap 'echo disabled > "$se/state"; : > "$se/root/ops"; exit 0' TERM INT
while true; do sleep 0.05; done
)BASH");
  CHECK(::chmod(loader.c_str(), 0755) == 0);
  CHECK(::setenv("EP_FAKE_SCHED_EXT", sched_ext.c_str(), 1) == 0);
  CHECK(::setenv("EP_FAKE_PIN", pin_dir.c_str(), 1) == 0);

  ep::SchedPaths paths;
  paths.state_dir = state_dir;
  paths.loader = loader.string();
  paths.sched_ext_dir = sched_ext.string();
  paths.pin_dir = pin_dir.string();

  const ep::Json intent{{"mode", "class"}};
  {
    ep::Executor executor(run_dir, std::make_unique<ep::SchedSkill>(paths),
                          ep::ExecutorOptions{true});
    std::string error;
    CHECK(executor.init(&error));
    ep::Json lease = executor.handle(ep::Json{{"operation", "lease.acquire"},
                                              {"request_id", "req-lease-seq"},
                                              {"lease_owner", "unit-owner-01"}});
    CHECK(lease["status"] == "OK");
    const std::uint64_t fence = lease["fence_epoch"].get<std::uint64_t>();
    const ep::Json envelope = make_envelope("policy.apply", intent, "unit-owner-01", fence, 0,
                                            "idem-seq-0001");
    ep::Json response = executor.handle(ep::Json{{"operation", "policy.apply"},
                                                  {"envelope", envelope},
                                                  {"intent", intent}});
    CHECK(response["status"] == "VERIFIED");

    ep::Applied applied;
    applied.data = ep::Json::parse(read_text_file(state_dir / "sched-runtime.json"));
    CHECK(applied.data["enable_seq"] == 101);

    ep::SchedSkill verifier(paths);
    // scx_enable_seq has exactly one increment site in the kernel and it lives
    // inside scx_enable(), so any value other than the one recorded at attach
    // means sched_ext was enabled again. For a same-named instance that is the
    // only signal left, because state and ops keep looking healthy.
    write_text_file(sched_ext / "enable_seq", "500\n");
    const auto moved = verifier.verify(applied, 0);
    CHECK(!moved.ok);
    CHECK(moved.detail.find("enable_seq moved") != std::string::npos);

    // A smaller counter is equally impossible and must also fail.
    write_text_file(sched_ext / "enable_seq", "5\n");
    CHECK(!verifier.verify(applied, 0).ok);

    // An unrelated takeover still fails on ops before reaching the counter.
    write_text_file(sched_ext / "enable_seq", "101\n");
    write_text_file(sched_ext / "root" / "ops", "thirdparty\n");
    const auto taken = verifier.verify(applied, 0);
    CHECK(!taken.ok);
    CHECK(taken.detail.find("ops does not contain eulerpilot") != std::string::npos);

    // Counter, ops, state and the live loader all unchanged is a healthy attach.
    write_text_file(sched_ext / "root" / "ops", "eulerpilot\n");
    CHECK(verifier.verify(applied, 0).ok);
  }
  std::filesystem::remove_all(dir);
}

void test_sched_enable_seq_publish_window() {
  const auto dir = make_temp_dir("sched-seq-window");
  const auto sched_ext = dir / "sched_ext";
  const auto pin_dir = dir / "pin";
  const auto state_dir = dir / "state";
  const auto run_dir = dir / "exec";
  std::filesystem::create_directories(sched_ext / "root");
  std::filesystem::create_directories(pin_dir);
  std::filesystem::create_directories(state_dir);
  write_text_file(sched_ext / "state", "disabled\n");
  write_text_file(sched_ext / "root" / "ops", "\n");
  write_text_file(sched_ext / "enable_seq", "0\n");

  // scx_enable() sets state=enabled well before it increments enable_seq, so the
  // loader reproduces that order: reading the counter immediately after attach
  // still returns the pre-enable value.
  const auto loader = dir / "fake-loader-slow-seq.sh";
  write_text_file(loader, R"BASH(#!/usr/bin/env bash
set -e
se="${EP_FAKE_SCHED_EXT:?}"
pin="${EP_FAKE_PIN:?}"
mkdir -p "$se/root" "$pin"
: > "$pin/cfg"
: > "$pin/class_map"
: > "$pin/stats"
echo eulerpilot > "$se/root/ops"
echo enabled > "$se/state"
( sleep 0.4; echo $(( $(cat "$se/enable_seq") + 1 )) > "$se/enable_seq" ) &
trap 'echo disabled > "$se/state"; : > "$se/root/ops"; exit 0' TERM INT
while true; do sleep 0.05; done
)BASH");
  CHECK(::chmod(loader.c_str(), 0755) == 0);
  CHECK(::setenv("EP_FAKE_SCHED_EXT", sched_ext.c_str(), 1) == 0);
  CHECK(::setenv("EP_FAKE_PIN", pin_dir.c_str(), 1) == 0);

  ep::SchedPaths paths;
  paths.state_dir = state_dir;
  paths.loader = loader.string();
  paths.sched_ext_dir = sched_ext.string();
  paths.pin_dir = pin_dir.string();

  const ep::Json intent{{"mode", "class"}};
  {
    ep::Executor executor(run_dir, std::make_unique<ep::SchedSkill>(paths),
                          ep::ExecutorOptions{true});
    std::string error;
    CHECK(executor.init(&error));
    ep::Json lease = executor.handle(ep::Json{{"operation", "lease.acquire"},
                                              {"request_id", "req-lease-window"},
                                              {"lease_owner", "unit-owner-01"}});
    const std::uint64_t fence = lease["fence_epoch"].get<std::uint64_t>();
    const ep::Json envelope = make_envelope("policy.apply", intent, "unit-owner-01", fence, 0,
                                            "idem-window-0001");
    const ep::Json response = executor.handle(ep::Json{{"operation", "policy.apply"},
                                                        {"envelope", envelope},
                                                        {"intent", intent}});
    CHECK(response["status"] == "VERIFIED");
    const ep::Json runtime =
        ep::Json::parse(read_text_file(state_dir / "sched-runtime.json"));
    CHECK(runtime["enable_seq"] == 1);
    CHECK(executor.handle(ep::Json{{"operation", "policy.status"},
                                   {"request_id", "req-status-window"}})
              .value("degraded", true) == false);
  }
  std::filesystem::remove_all(dir);
}

void test_sched_liveness_takeover() {
  const auto dir = make_temp_dir("sched-liveness");
  const auto sched_ext = dir / "sched_ext";
  const auto pin_dir = dir / "pin";
  const auto state_dir = dir / "state";
  const auto run_dir = dir / "exec";
  std::filesystem::create_directories(sched_ext / "root");
  std::filesystem::create_directories(pin_dir);
  std::filesystem::create_directories(state_dir);
  write_text_file(sched_ext / "state", "disabled\n");
  write_text_file(sched_ext / "root" / "ops", "\n");
  write_text_file(sched_ext / "enable_seq", "0\n");

  const auto loader = dir / "fake-loader-alive.sh";
  write_text_file(loader, R"BASH(#!/usr/bin/env bash
set -e
se="${EP_FAKE_SCHED_EXT:?}"
pin="${EP_FAKE_PIN:?}"
mkdir -p "$se/root" "$pin"
: > "$pin/cfg"
: > "$pin/class_map"
: > "$pin/stats"
echo eulerpilot > "$se/root/ops"
echo $(( $(cat "$se/enable_seq") + 1 )) > "$se/enable_seq"
echo enabled > "$se/state"
trap 'echo disabled > "$se/state"; : > "$se/root/ops"; exit 0' TERM INT
while true; do sleep 0.05; done
)BASH");
  CHECK(::chmod(loader.c_str(), 0755) == 0);
  CHECK(::setenv("EP_FAKE_SCHED_EXT", sched_ext.c_str(), 1) == 0);
  CHECK(::setenv("EP_FAKE_PIN", pin_dir.c_str(), 1) == 0);

  ep::SchedPaths paths;
  paths.state_dir = state_dir;
  paths.loader = loader.string();
  paths.sched_ext_dir = sched_ext.string();
  paths.pin_dir = pin_dir.string();

  const ep::Json intent{{"mode", "class"}};
  {
    ep::Executor executor(run_dir, std::make_unique<ep::SchedSkill>(paths),
                          ep::ExecutorOptions{true});
    std::string error;
    CHECK(executor.init(&error));
    ep::Json lease = executor.handle(ep::Json{{"operation", "lease.acquire"},
                                              {"request_id", "req-lease-live"},
                                              {"lease_owner", "unit-owner-01"}});
    const std::uint64_t fence = lease["fence_epoch"].get<std::uint64_t>();
    const ep::Json envelope = make_envelope("policy.apply", intent, "unit-owner-01", fence, 0,
                                            "idem-live-0001");
    ep::Json response = executor.handle(ep::Json{{"operation", "policy.apply"},
                                                  {"envelope", envelope},
                                                  {"intent", intent}});
    CHECK(response["status"] == "VERIFIED");

    const auto status = [&executor]() {
      return executor.handle(ep::Json{{"operation", "policy.status"},
                                      {"request_id", "req-status-live"}});
    };
    const auto ready = status();
    CHECK(ready.value("status", "") == "OK");
    CHECK(ready.value("degraded", true) == false);

    // One poll landing on a transient read must not tear down a healthy effect.
    write_text_file(sched_ext / "root" / "ops", "thirdparty\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    write_text_file(sched_ext / "root" / "ops", "eulerpilot\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(1400));
    CHECK(status().value("degraded", true) == false);
    CHECK(read_text_file(sched_ext / "state") == "enabled");

    // A same-named instance taking over leaves state and ops looking healthy, so
    // the enable counter is the only thing that reveals it; the bounded fallback
    // must still fire.
    write_text_file(sched_ext / "enable_seq", "99\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(4500));
    const auto after = status();
    CHECK(after.value("degraded", false) == true);
    const std::string fallback = after.value("last_fallback", std::string());
    CHECK(fallback.find("scheduler lost") != std::string::npos);
    CHECK(fallback.find("enable_seq moved") != std::string::npos);
    CHECK(read_text_file(sched_ext / "state") == "disabled");
    CHECK(!std::filesystem::exists(state_dir / "sched-runtime.json"));
  }
  std::filesystem::remove_all(dir);
}

void test_startup_degraded_reason_surfaced() {
  const auto dir = make_temp_dir("startup-degraded");
  const auto sched_ext = dir / "sched_ext";
  const auto pin_root = dir / "pin-root";
  const auto state_dir = dir / "state";
  const auto run_dir = dir / "exec";
  std::filesystem::create_directories(sched_ext / "root");
  std::filesystem::create_directories(pin_root);
  std::filesystem::create_directories(state_dir);
  // Another v2 instance already owns the scheduler: eulerpilot is registered, but
  // this instance has no runtime marker of its own. Startup must refuse and, after
  // the 2026-10-22 SP3 takeover run, must be able to say why.
  write_text_file(sched_ext / "state", "enabled\n");
  write_text_file(sched_ext / "root" / "ops", "eulerpilot\n");
  write_text_file(sched_ext / "enable_seq", "7\n");

  ep::SchedPaths paths;
  paths.state_dir = state_dir;
  paths.sched_ext_dir = sched_ext.string();
  paths.pin_root = pin_root.string();
  paths.loader = "/bin/false";  // never reached: apply is refused before spawn

  ep::Executor executor(run_dir, std::make_unique<ep::SchedSkill>(paths),
                        ep::ExecutorOptions{true});
  std::string error;
  CHECK(executor.init(&error));
  const auto status = executor.handle(ep::Json{{"operation", "policy.status"},
                                               {"request_id", "req-startup-degraded"}});
  CHECK(status.value("degraded", false) == true);
  const std::string reason = status.value("startup_degraded_reason", std::string());
  CHECK(reason.find("eulerpilot is attached but no runtime marker exists") != std::string::npos);

  ep::Json lease = executor.handle(ep::Json{{"operation", "lease.acquire"},
                                            {"request_id", "req-lease-degraded"},
                                            {"lease_owner", "unit-owner-01"}});
  const std::uint64_t fence = lease["fence_epoch"].get<std::uint64_t>();
  const ep::Json intent{{"mode", "class"}};
  const ep::Json envelope = make_envelope("policy.apply", intent, "unit-owner-01", fence, 0,
                                          "idem-degraded-0001");
  const ep::Json response = executor.handle(ep::Json{{"operation", "policy.apply"},
                                                      {"envelope", envelope},
                                                      {"intent", intent}});
  CHECK(response["status"] == "RECOVERY_REQUIRED");
  const std::string detail = response.contains("error") && response["error"].contains("detail")
                                 ? response["error"].at("detail").get<std::string>()
                                 : std::string();
  CHECK(detail.find("no runtime marker") != std::string::npos);
  std::filesystem::remove_all(dir);
}

void test_sched_helper_timeout() {
  const auto dir = make_temp_dir("sched-helper-timeout");
  const auto sched_ext = dir / "sched_ext";
  const auto pin_dir = dir / "pin";
  const auto state_dir = dir / "state";
  const auto run_dir = dir / "exec";
  std::filesystem::create_directories(sched_ext / "root");
  std::filesystem::create_directories(pin_dir);
  std::filesystem::create_directories(state_dir);
  write_text_file(sched_ext / "state", "disabled\n");
  write_text_file(sched_ext / "root" / "ops", "\n");
  write_text_file(sched_ext / "enable_seq", "0\n");

  const auto loader = dir / "fake-loader.sh";
  write_text_file(loader, R"BASH(#!/usr/bin/env bash
set -e
se="${EP_FAKE_SCHED_EXT:?}"
pin="${EP_FAKE_PIN:?}"
mkdir -p "$se/root" "$pin"
: > "$pin/cfg"
: > "$pin/class_map"
: > "$pin/stats"
echo eulerpilot > "$se/root/ops"
echo $(( $(cat "$se/enable_seq") + 1 )) > "$se/enable_seq"
echo enabled > "$se/state"
trap 'echo disabled > "$se/state"; : > "$se/root/ops"; exit 0' TERM INT
while true; do sleep 0.05; done
)BASH");
  CHECK(::chmod(loader.c_str(), 0755) == 0);

  const auto hang = dir / "hang-ctrl.sh";
  write_text_file(hang, "#!/usr/bin/env bash\nsleep 30\n");
  CHECK(::chmod(hang.c_str(), 0755) == 0);

  CHECK(::setenv("EP_FAKE_SCHED_EXT", sched_ext.c_str(), 1) == 0);
  CHECK(::setenv("EP_FAKE_PIN", pin_dir.c_str(), 1) == 0);

  ep::SchedPaths paths;
  paths.state_dir = state_dir;
  paths.loader = loader.string();
  paths.sched_ext_dir = sched_ext.string();
  paths.pin_dir = pin_dir.string();
  paths.ctrl = hang.string();
  paths.ctrl_timeout_ms = 200;

  ep::Executor executor(run_dir, std::make_unique<ep::SchedSkill>(paths),
                        ep::ExecutorOptions{true});
  std::string error;
  CHECK(executor.init(&error));
  ep::Json lease = executor.handle(ep::Json{{"operation", "lease.acquire"},
                                            {"request_id", "req-lease-timeout"},
                                            {"lease_owner", "unit-owner-01"}});
  CHECK(lease["status"] == "OK");
  const std::uint64_t fence = lease["fence_epoch"].get<std::uint64_t>();
  const ep::Json intent{{"mode", "class"},
                        {"parameters", ep::Json{{"lat_slice_ns", 2000000}}}};
  const ep::Json envelope = make_envelope("policy.apply", intent, "unit-owner-01", fence, 0,
                                          "idem-helper-timeout");
  const auto start = std::chrono::steady_clock::now();
  ep::Json response = executor.handle(ep::Json{{"operation", "policy.apply"},
                                                {"envelope", envelope},
                                                {"intent", intent}});
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start)
                           .count();
  CHECK(response["status"] != "VERIFIED");
  CHECK(response["error"]["message"].get<std::string>().find("timed out") !=
        std::string::npos);
  CHECK(elapsed < 2000);
  CHECK(read_text_file(sched_ext / "state") == "disabled");
  CHECK(!std::filesystem::exists(pin_dir) || std::filesystem::is_empty(pin_dir));
  CHECK(!std::filesystem::exists(state_dir / "sched-runtime.json"));
  std::filesystem::remove_all(dir);
}

void test_sched_refresh_budget() {
  const auto dir = make_temp_dir("refresh-budget");
  const auto hang = dir / "hang.sh";
  write_text_file(hang, "#!/usr/bin/env bash\nsleep 30\n");
  CHECK(::chmod(hang.c_str(), 0755) == 0);
  ep::SchedPaths paths;
  paths.state_dir = dir / "state";
  paths.ctrl = hang.string();
  paths.ctrl_timeout_ms = 2000;
  ep::SchedSkill skill(paths);
  ep::Applied applied;
  applied.data = ep::Json{{"classified", ep::Json::array(
      {ep::Json{{"pid", 1}, {"class", "BG"}}, ep::Json{{"pid", 2}, {"class", "LAT"}},
       ep::Json{{"pid", 3}, {"class", "CACHE"}}})}};
  const auto start = std::chrono::steady_clock::now();
  ep::RefreshContext context{ep::now_epoch_ms() + 200};
  std::string error;
  CHECK(!skill.refresh(applied, {}, context, &error));
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start)
                           .count();
  CHECK(elapsed < 600);
  CHECK(error.find("timed out") != std::string::npos ||
        error.find("budget") != std::string::npos);
  std::cout << "refresh_budget_ms=" << elapsed << "\n";
  std::filesystem::remove_all(dir);
}

void test_sched_eof_budget() {
  const auto dir = make_temp_dir("eof-budget");
  const auto eof = dir / "eof.sh";
  write_text_file(eof, "#!/usr/bin/env bash\nexec 1>&- 2>&-\nsleep 30\n");
  CHECK(::chmod(eof.c_str(), 0755) == 0);
  ep::SchedPaths paths;
  paths.state_dir = dir / "state";
  paths.ctrl = eof.string();
  paths.ctrl_timeout_ms = 2000;
  ep::SchedSkill skill(paths);
  ep::Applied applied;
  applied.data = ep::Json{{"classified", ep::Json::array(
      {ep::Json{{"pid", 1}, {"class", "BG"}}})}};
  const auto start = std::chrono::steady_clock::now();
  ep::RefreshContext context{ep::now_epoch_ms() + 200};
  std::string error;
  CHECK(!skill.refresh(applied, {}, context, &error));
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start)
                           .count();
  CHECK(elapsed < 600);
  CHECK(error.find("did not exit") != std::string::npos ||
        error.find("timed out") != std::string::npos);
  std::cout << "eof_budget_ms=" << elapsed << "\n";
  std::filesystem::remove_all(dir);
}

void test_sched_abort_foreign_pin() {
  const auto dir = make_temp_dir("abort-foreign");
  const auto sched_ext = dir / "sched_ext";
  const auto pin_dir = dir / "pin";
  const auto state_dir = dir / "state";
  const auto run_dir = dir / "exec";
  std::filesystem::create_directories(sched_ext / "root");
  std::filesystem::create_directories(pin_dir);
  std::filesystem::create_directories(state_dir);
  write_text_file(sched_ext / "state", "disabled\n");
  write_text_file(sched_ext / "root" / "ops", "\n");
  write_text_file(sched_ext / "enable_seq", "0\n");

  const auto loader = dir / "fake-loader.sh";
  write_text_file(loader, R"BASH(#!/usr/bin/env bash
set -e
se="${EP_FAKE_SCHED_EXT:?}"
pin="${EP_FAKE_PIN:?}"
mkdir -p "$se/root" "$pin"
: > "$pin/cfg"
: > "$pin/class_map"
: > "$pin/stats"
( sleep 0.6; : > "$pin/foreign" ) &
echo eulerpilot > "$se/root/ops"
echo $(( $(cat "$se/enable_seq") + 1 )) > "$se/enable_seq"
echo enabled > "$se/state"
trap 'echo disabled > "$se/state"; : > "$se/root/ops"; exit 0' TERM INT
while true; do sleep 0.05; done
)BASH");
  CHECK(::chmod(loader.c_str(), 0755) == 0);
  const auto hang = dir / "hang.sh";
  write_text_file(hang, "#!/usr/bin/env bash\nsleep 30\n");
  CHECK(::chmod(hang.c_str(), 0755) == 0);
  CHECK(::setenv("EP_FAKE_SCHED_EXT", sched_ext.c_str(), 1) == 0);
  CHECK(::setenv("EP_FAKE_PIN", pin_dir.c_str(), 1) == 0);

  ep::SchedPaths paths;
  paths.state_dir = state_dir;
  paths.loader = loader.string();
  paths.sched_ext_dir = sched_ext.string();
  paths.pin_dir = pin_dir.string();
  paths.ctrl = hang.string();
  paths.ctrl_timeout_ms = 800;
  ep::Executor executor(run_dir, std::make_unique<ep::SchedSkill>(paths),
                        ep::ExecutorOptions{true});
  std::string error;
  CHECK(executor.init(&error));
  ep::Json lease = executor.handle(ep::Json{{"operation", "lease.acquire"},
                                            {"request_id", "req-lease-foreign"},
                                            {"lease_owner", "unit-owner-01"}});
  const std::uint64_t fence = lease["fence_epoch"].get<std::uint64_t>();
  const ep::Json intent{{"mode", "class"},
                        {"parameters", ep::Json{{"lat_slice_ns", 2000000}}}};
  const ep::Json envelope = make_envelope("policy.apply", intent, "unit-owner-01", fence, 0,
                                          "idem-foreign-pin");
  ep::Json response = executor.handle(ep::Json{{"operation", "policy.apply"},
                                                {"envelope", envelope},
                                                {"intent", intent}});
  CHECK(response["status"] == "RECOVERY_REQUIRED");
  CHECK(std::filesystem::exists(pin_dir / "foreign"));
  CHECK(std::filesystem::exists(pin_dir / "cfg"));
  CHECK(!std::filesystem::exists(state_dir / "sched-runtime.json"));
  std::filesystem::remove_all(dir);
}

void test_journal_fault() {
  const auto dir = make_temp_dir("journal-fault");
  {
    ep::Journal journal(dir);
    std::string error;
    CHECK(journal.open(&error));
    ep::JournalRecord first;
    first.txn = "txn-fault";
    first.type = "PREPARED";
    CHECK(journal.append(first, &error));
    journal.set_fault_after(1);
    ep::JournalRecord second = first;
    second.type = "APPLYING";
    CHECK(!journal.append(second, &error));
    CHECK(journal.broken());
    ep::JournalRecord third = first;
    third.type = "VERIFIED";
    CHECK(!journal.append(third, &error));
    CHECK(journal.size() == 1);
  }
  {
    ep::Journal reopened(dir);
    std::string error;
    CHECK(reopened.open(&error));
    CHECK(reopened.size() == 1);
    ep::JournalRecord next;
    next.txn = "txn-after-restart";
    next.type = "PREPARED";
    CHECK(reopened.append(next, &error));
    CHECK(reopened.size() == 2);
  }
  std::filesystem::remove_all(dir);
}

void test_wal_faults() {
  // PREPARED append fails: no effect, journal broken, next apply refused.
  {
    const auto dir = make_temp_dir("wal-prepared-fail");
    ep::Executor executor(dir, std::make_unique<ep::MockSkill>(dir / "mock"),
                          ep::ExecutorOptions{true, 0});
    std::string error;
    CHECK(executor.init(&error));
    ep::Json lease = executor.handle(ep::Json{{"operation", "lease.acquire"},
                                              {"request_id", "req-wal-1"},
                                              {"lease_owner", "unit-owner-01"}});
    const std::uint64_t fence = lease["fence_epoch"].get<std::uint64_t>();
    const ep::Json intent{{"value", 7}};
    const ep::Json envelope =
        make_envelope("policy.apply", intent, "unit-owner-01", fence, 0, "idem-wal-1");
    ep::Json response = executor.handle(ep::Json{{"operation", "policy.apply"},
                                                 {"envelope", envelope},
                                                 {"intent", intent}});
    CHECK(response["status"] != "VERIFIED");
    bool exists = true;
    CHECK(read_mock_value(dir, &exists) == 0);
    CHECK(!exists);
    const ep::Json second = make_envelope("policy.apply", intent, "unit-owner-01", fence, 0,
                                          "idem-wal-1b");
    ep::Json refused = executor.handle(ep::Json{{"operation", "policy.apply"},
                                                {"envelope", second},
                                                {"intent", intent}});
    CHECK(refused["status"] == "RECOVERY_REQUIRED");
    std::filesystem::remove_all(dir);
  }

  // VERIFYING append fails after the effect: compensate rolls it back and the
  // executor reports audit pending, not VERIFIED.
  {
    const auto dir = make_temp_dir("wal-verifying-fail");
    ep::Executor executor(dir, std::make_unique<ep::MockSkill>(dir / "mock"),
                          ep::ExecutorOptions{true, 2});
    std::string error;
    CHECK(executor.init(&error));
    ep::Json lease = executor.handle(ep::Json{{"operation", "lease.acquire"},
                                              {"request_id", "req-wal-2"},
                                              {"lease_owner", "unit-owner-01"}});
    const std::uint64_t fence = lease["fence_epoch"].get<std::uint64_t>();
    const ep::Json intent{{"value", 7}};
    const ep::Json envelope =
        make_envelope("policy.apply", intent, "unit-owner-01", fence, 0, "idem-wal-2");
    ep::Json response = executor.handle(ep::Json{{"operation", "policy.apply"},
                                                 {"envelope", envelope},
                                                 {"intent", intent}});
    CHECK(response["status"] == "DEGRADED");
    bool exists = true;
    CHECK(read_mock_value(dir, &exists) == 0);
    CHECK(!exists);
    std::filesystem::remove_all(dir);
  }

  // ROLLBACK_PREPARED append fails: the effect is still detached, audit pending.
  {
    const auto dir = make_temp_dir("wal-rb-prepared-fail");
    ep::Executor executor(dir, std::make_unique<ep::MockSkill>(dir / "mock"),
                          ep::ExecutorOptions{true, 6});
    std::string error;
    CHECK(executor.init(&error));
    ep::Json lease = executor.handle(ep::Json{{"operation", "lease.acquire"},
                                              {"request_id", "req-wal-3"},
                                              {"lease_owner", "unit-owner-01"}});
    const std::uint64_t fence = lease["fence_epoch"].get<std::uint64_t>();
    const ep::Json intent{{"value", 7}};
    const ep::Json envelope =
        make_envelope("policy.apply", intent, "unit-owner-01", fence, 0, "idem-wal-3");
    ep::Json applied = executor.handle(ep::Json{{"operation", "policy.apply"},
                                                 {"envelope", envelope},
                                                 {"intent", intent}});
    CHECK(applied["status"] == "VERIFIED");
    const ep::Json rollback_of{{"apply_action_id", applied["action_id"]},
                               {"original_transaction_id", applied["transaction_id"]},
                               {"snapshot_id", applied["snapshot_id"]}};
    const ep::Json rb = make_envelope("policy.rollback", intent, "unit-owner-01", fence, 1,
                                      "idem-wal-3rb", rollback_of);
    ep::Json rolled = executor.handle(ep::Json{{"operation", "policy.rollback"},
                                                {"envelope", rb}});
    CHECK(rolled["status"] == "DEGRADED");
    bool exists = true;
    CHECK(read_mock_value(dir, &exists) == 0);
    CHECK(!exists);
    std::filesystem::remove_all(dir);
  }

  // ROLLED_BACK append fails after the effect is gone: DEGRADED, no false commit.
  {
    const auto dir = make_temp_dir("wal-rb-completed-fail");
    ep::Executor executor(dir, std::make_unique<ep::MockSkill>(dir / "mock"),
                          ep::ExecutorOptions{true, 7});
    std::string error;
    CHECK(executor.init(&error));
    ep::Json lease = executor.handle(ep::Json{{"operation", "lease.acquire"},
                                              {"request_id", "req-wal-4"},
                                              {"lease_owner", "unit-owner-01"}});
    const std::uint64_t fence = lease["fence_epoch"].get<std::uint64_t>();
    const ep::Json intent{{"value", 7}};
    const ep::Json envelope =
        make_envelope("policy.apply", intent, "unit-owner-01", fence, 0, "idem-wal-4");
    ep::Json applied = executor.handle(ep::Json{{"operation", "policy.apply"},
                                                 {"envelope", envelope},
                                                 {"intent", intent}});
    CHECK(applied["status"] == "VERIFIED");
    const ep::Json rollback_of{{"apply_action_id", applied["action_id"]},
                               {"original_transaction_id", applied["transaction_id"]},
                               {"snapshot_id", applied["snapshot_id"]}};
    const ep::Json rb = make_envelope("policy.rollback", intent, "unit-owner-01", fence, 1,
                                      "idem-wal-4rb", rollback_of);
    ep::Json rolled = executor.handle(ep::Json{{"operation", "policy.rollback"},
                                                {"envelope", rb}});
    CHECK(rolled["status"] == "DEGRADED");
    bool exists = true;
    CHECK(read_mock_value(dir, &exists) == 0);
    CHECK(!exists);
    std::filesystem::remove_all(dir);
  }
}

void test_feature_window() {
  ep::FeatureWindow window(4);
  window.sample();
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  window.sample();
  const ep::Json snapshot = window.snapshot();
  CHECK(snapshot["samples"] == 2);
  CHECK(snapshot["metrics"]["load1"]["valid"] == true);
  CHECK(snapshot["metrics"]["cpu_busy_pct"]["samples"] >= 1);
  CHECK(snapshot["metrics"]["run_delay_ns"]["valid"] == false);
  CHECK(!snapshot["metrics"]["run_delay_ns"]["missing"].get<std::string>().empty());
}

ep::Json feature_metric(bool valid, double avg) {
  return ep::Json{{"valid", valid}, {"source", "test"}, {"samples", 3},
                  {"age_ms", 0},     {"min", avg},      {"avg", avg},
                  {"max", avg}};
}

ep::Json feature_snapshot(bool sufficient, bool cpu_valid, double cpu, bool load_valid,
                          double load, bool psi_valid, double psi) {
  return ep::Json{
      {"sufficient", sufficient},
      {"age_ms", 0},
      {"metrics",
       ep::Json{{"cpu_busy_pct", feature_metric(cpu_valid, cpu)},
                {"load1", feature_metric(load_valid, load)},
                {"psi_cpu_some_avg10", feature_metric(psi_valid, psi)}}}};
}

void test_rule_router() {
  ep::RuleRouter immediate(0);
  ep::Json decision = immediate.decide(
      feature_snapshot(false, false, 0.0, false, 0.0, false, 0.0), 1000);
  CHECK(decision["expert"] == "safe-basic");
  CHECK(decision["degraded"] == true);

  decision = immediate.decide(feature_snapshot(true, true, 50.0, true, 1.0, true, 30.0),
                              2000);
  CHECK(decision["expert"] == "latency-balanced");
  CHECK(decision["switched"] == true);

  decision = immediate.decide(feature_snapshot(true, true, 95.0, true, 4.0, true, 1.0),
                              3000);
  CHECK(decision["expert"] == "throughput-balanced");

  ep::RuleRouter delayed(1000);
  ep::Json pending = delayed.decide(
      feature_snapshot(true, true, 50.0, true, 1.0, true, 30.0), 0);
  CHECK(pending["expert"] == "safe-basic");
  CHECK(pending["pending"] == true);
  pending = delayed.decide(feature_snapshot(true, true, 50.0, true, 1.0, true, 30.0), 500);
  CHECK(pending["expert"] == "safe-basic");
  pending = delayed.decide(feature_snapshot(true, true, 50.0, true, 1.0, true, 30.0), 1100);
  CHECK(pending["expert"] == "latency-balanced");
  CHECK(pending["switched"] == true);
}

ep::SloInput slo_input(const ep::Json& features, const std::string& expert) {
  ep::SloInput in;
  in.features = features;
  in.expert = expert;
  in.now_ms = 20000;
  in.max_observation_age_ms = 1000;
  return in;
}

ep::ServiceObservation service_observation(const std::string& expert, std::int64_t observed_at_ms,
                                           double p99_ms, double bg_units, double bg_wait_ms) {
  ep::ServiceObservation obs;
  obs.workload = "svc-unit";
  obs.observed_at_ms = observed_at_ms;
  obs.window_ms = 5000;
  obs.expert = expert;
  obs.has_foreground_p99 = true;
  obs.foreground_p99_ms = p99_ms;
  obs.has_bg_work_units = true;
  obs.bg_work_units_per_sec = bg_units;
  obs.has_bg_max_wait = true;
  obs.bg_max_wait_ms = bg_wait_ms;
  return obs;
}

void test_slo_gate() {
  ep::SloGate gate;
  ep::Json decision = gate.evaluate(
      slo_input(feature_snapshot(false, false, 0.0, false, 0.0, false, 0.0), "latency-balanced"));
  CHECK(decision["allowed"] == false);
  CHECK(decision["code"] == "FEATURES_INSUFFICIENT");

  decision = gate.evaluate(slo_input(feature_snapshot(true, true, 50.0, true, 1.0, true, 30.0),
                                     "throughput-balanced"));
  CHECK(decision["allowed"] == false);
  CHECK(decision["code"] == "PSI_PRESSURE_TOO_HIGH");

  decision = gate.evaluate(slo_input(feature_snapshot(true, true, 50.0, true, 1.0, true, 30.0),
                                     "latency-balanced"));
  CHECK(decision["allowed"] == true);

  decision = gate.evaluate(slo_input(feature_snapshot(true, true, 50.0, true, 1.0, false, 0.0),
                                     "latency-balanced"));
  CHECK(decision["allowed"] == false);
  CHECK(decision["code"] == "PSI_FEATURE_MISSING");

  decision = gate.evaluate(slo_input(feature_snapshot(true, true, 50.0, true, 1.0, false, 0.0),
                                     "safe-basic"));
  CHECK(decision["allowed"] == true);

  decision = gate.evaluate(slo_input(feature_snapshot(true, true, 50.0, true, 1.0, true, 1.0),
                                     "cache-local"));
  CHECK(decision["allowed"] == false);
  CHECK(decision["code"] == "LLC_FEATURE_MISSING");

  ep::Json stale = feature_snapshot(true, true, 50.0, true, 1.0, true, 30.0);
  stale["age_ms"] = 5000;
  decision = gate.evaluate(slo_input(stale, "latency-balanced"));
  CHECK(decision["allowed"] == false);
  CHECK(decision["code"] == "FEATURES_STALE");
}

void test_service_slo() {
  ep::SloGate gate;
  const ep::Json good_features = feature_snapshot(true, true, 50.0, true, 1.0, true, 30.0);
  const ep::Json safety{{"min_bg_work_units_per_sec", 1.0},
                        {"max_bg_wait_ms", 1000.0},
                        {"evaluation_min_bg_progress_ratio", 0.5},
                        {"quiet_max_regression_pct", 5.0}};

  // Without telemetry the gate must say "not evaluated", never "passed".
  ep::Json decision = gate.evaluate(slo_input(good_features, "latency-balanced"));
  CHECK(decision["allowed"] == true);
  CHECK(decision["service_evidence"] == "absent");

  ep::SloInput measured = slo_input(good_features, "latency-balanced");
  measured.has_observation = true;
  measured.safety = safety;
  measured.observation = service_observation("latency-balanced", 18000, 1.6, 900.0, 40.0);
  decision = gate.evaluate(measured);
  CHECK(decision["allowed"] == true);
  CHECK(decision["service_evidence"] == "present");

  // A measured floor breach outranks the host-feature verdict: it is the more
  // specific evidence about the workload the operator cares about.
  ep::SloInput starving = slo_input(
      feature_snapshot(false, false, 0.0, false, 0.0, false, 0.0), "latency-balanced");
  starving.has_observation = true;
  starving.safety = safety;
  starving.observation = service_observation("latency-balanced", 18000, 1.6, 0.5, 40.0);
  decision = gate.evaluate(starving);
  CHECK(decision["allowed"] == false);
  CHECK(decision["code"] == "BG_PROGRESS_FLOOR");

  ep::SloInput waiting = measured;
  waiting.observation = service_observation("latency-balanced", 18000, 1.6, 900.0, 1500.0);
  decision = gate.evaluate(waiting);
  CHECK(decision["allowed"] == false);
  CHECK(decision["code"] == "BG_WAIT_EXCEEDED");

  ep::SloInput outdated = measured;
  outdated.observation = service_observation("latency-balanced", 1000, 1.6, 900.0, 40.0);
  decision = gate.evaluate(outdated);
  CHECK(decision["allowed"] == false);
  CHECK(decision["code"] == "SERVICE_OBSERVATION_STALE");

  ep::SloInput condemned = measured;
  condemned.adverse["latency-balanced"] = "BG_PROGRESS_FLOOR: measured 0.500000 below floor";
  decision = gate.evaluate(condemned);
  CHECK(decision["allowed"] == false);
  CHECK(decision["code"] == "SERVICE_EVIDENCE_DENIED");
  // Evidence is per expert: another档 is still negotiable.
  condemned.expert = "safe-basic";
  CHECK(gate.evaluate(condemned)["allowed"] == true);

  // Nothing declared means nothing invented.
  ep::SloInput undeclared = measured;
  undeclared.safety = ep::Json::object();
  undeclared.observation = service_observation("latency-balanced", 18000, 1.6, 0.0, 9000.0);
  CHECK(gate.evaluate(undeclared)["allowed"] == true);

  // Observation contract.
  ep::ServiceObservation parsed;
  std::string error;
  CHECK(ep::parse_service_observation(
      ep::Json{{"workload", "svc"},
               {"observed_at_ms", 1000},
               {"window_ms", 2000},
               {"expert", "safe-basic"},
               {"foreground", ep::Json{{"p99_latency_ms", 2.5}}}},
      &parsed, &error));
  CHECK(parsed.has_foreground_p99);
  CHECK(!parsed.has_bg_work_units);
  CHECK(parsed.expert == "safe-basic");

  CHECK(!ep::parse_service_observation(
      ep::Json{{"workload", "svc"},
               {"observed_at_ms", 1000},
               {"window_ms", 2000},
               {"expert", "safe-basic"},
               {"foreground", ep::Json{{"p50_latency_ms", 2.5}}}},
      &parsed, &error));
  CHECK(!error.empty());
  error.clear();
  CHECK(!ep::parse_service_observation(
      ep::Json{{"workload", "svc"},
               {"observed_at_ms", 1000},
               {"window_ms", 50},
               {"expert", "safe-basic"},
               {"background", ep::Json{{"work_units_per_sec", 3.0}}}},
      &parsed, &error));
  error.clear();
  CHECK(!ep::parse_service_observation(
      ep::Json{{"workload", "svc"},
               {"observed_at_ms", 1000},
               {"window_ms", 2000},
               {"expert", "made-up"},
               {"background", ep::Json{{"work_units_per_sec", 3.0}}}},
      &parsed, &error));
  error.clear();
  CHECK(!ep::parse_service_observation(
      ep::Json{{"workload", "svc"},
               {"observed_at_ms", 1000},
               {"window_ms", 2000},
               {"expert", "safe-basic"}},
      &parsed, &error));

  // Switch outcome: the measurement after a change decides whether it hurt.
  const ep::ServiceObservation before =
      service_observation("latency-balanced", 1000, 1.6, 1000.0, 40.0);
  const ep::ServiceNoise silent;  // no history yet -> every declared check runs
  CHECK(ep::evaluate_switch_outcome(safety, before,
                                    service_observation("safe-basic", 6000, 1.6, 400.0, 40.0),
                                    silent)
            .code == "BG_PROGRESS_REGRESSED");
  CHECK(ep::evaluate_switch_outcome(safety, before,
                                    service_observation("safe-basic", 6000, 1.6, 600.0, 40.0),
                                    silent)
            .code == "");
  CHECK(ep::evaluate_switch_outcome(safety, before,
                                    service_observation("safe-basic", 6000, 1.6, 60.0, 40.0),
                                    silent)
            .code == "BG_PROGRESS_REGRESSED");
  CHECK(ep::evaluate_switch_outcome(safety, before,
                                    service_observation("safe-basic", 6000, 1.6, 1000.0, 2000.0),
                                    silent)
            .code == "BG_WAIT_EXCEEDED");
  CHECK(ep::evaluate_switch_outcome(safety, before,
                                    service_observation("safe-basic", 6000, 1.8, 1000.0, 40.0),
                                    silent)
            .code == "FOREGROUND_REGRESSED");
  CHECK(ep::evaluate_switch_outcome(ep::Json::object(), before,
                                    service_observation("safe-basic", 6000, 99.0, 0.0, 9000.0),
                                    silent)
            .code == "");

  // Resolution floor: the SP3 characterisation measured p99 moving 3.8-20% between
  // windows of the same state, so a 5% budget cannot be judged. The verdict must
  // say "not run", never pretend the check passed.
  ep::ServiceNoise loud;
  loud.p99_pct = 15.0;
  loud.bg_progress_pct = 1.0;
  const auto unresolvable = ep::evaluate_switch_outcome(
      safety, before, service_observation("safe-basic", 6000, 1.9, 1000.0, 40.0), loud);
  CHECK(unresolvable.code.empty());
  CHECK(unresolvable.skipped_foreground);
  CHECK(unresolvable.detail.find("not run") != std::string::npos);
  // Absolute limits are single-window comparisons, so noise does not excuse them.
  CHECK(ep::evaluate_switch_outcome(safety, before,
                                    service_observation("safe-basic", 6000, 9.9, 0.01, 40.0), loud)
            .code == "BG_PROGRESS_FLOOR");
  CHECK(ep::evaluate_switch_outcome(safety, before,
                                    service_observation("safe-basic", 6000, 1.6, 1000.0, 2000.0),
                                    loud)
            .code == "BG_WAIT_EXCEEDED");
  // A progress contract finer than the progress measurement is skipped the same way.
  ep::Json tight_ratio = safety;
  tight_ratio["evaluation_min_bg_progress_ratio"] = 0.995;  // asks to resolve 0.5%
  const auto ratio_unrunnable = ep::evaluate_switch_outcome(
      tight_ratio, before, service_observation("safe-basic", 6000, 1.6, 999.0, 40.0), loud);
  CHECK(ratio_unrunnable.skipped_ratio);
  CHECK(ratio_unrunnable.code.empty());
  // Absolute forward ceiling: single-window fact, so noise excuses nothing.
  ep::Json with_ceiling = safety;
  with_ceiling["max_foreground_p99_ms"] = 5.0;
  ep::SloInput over = measured;
  over.safety = with_ceiling;
  over.observation = service_observation("latency-balanced", 18000, 20.0, 900.0, 40.0);
  CHECK(gate.evaluate(over).value("code", "") == "FOREGROUND_P99_EXCEEDED");
  CHECK(gate.evaluate(over)["allowed"] == false);
  over.observation = service_observation("latency-balanced", 18000, 4.9, 900.0, 40.0);
  CHECK(gate.evaluate(over)["allowed"] == true);
  // Undeclared means unenforced: the same 20ms measurement passes with no ceiling.
  CHECK(gate.evaluate(measured)["allowed"] == true);
  // With no foreground number at all the check reports "not evaluated".
  ep::SloInput no_fg = measured;
  no_fg.safety = with_ceiling;
  no_fg.observation = service_observation("latency-balanced", 18000, 0.0, 900.0, 40.0);
  no_fg.observation.has_foreground_p99 = false;
  CHECK(gate.evaluate(no_fg)["allowed"] == true);
  CHECK(gate.evaluate(no_fg)["service_evidence"] == "present");

  // If progress itself were that coarse (80% window-to-window), even a 50%
  // sensitivity cannot be honoured: the check is reported as unrunnable, and the
  // absolute floor is what still protects the background role.
  ep::ServiceNoise jittery_progress = loud;
  jittery_progress.bg_progress_pct = 80.0;
  CHECK(ep::evaluate_switch_outcome(safety, before,
                                    service_observation("safe-basic", 6000, 1.6, 400.0, 40.0),
                                    jittery_progress)
            .skipped_ratio);
  CHECK(!ep::evaluate_switch_outcome(safety, before,
                                     service_observation("safe-basic", 6000, 1.6, 999.0, 40.0),
                                     silent)
             .skipped_ratio);
}

void test_service_observe_rpc() {
  const auto dir = make_temp_dir("observe");
  {
    ep::Executor executor(dir, std::make_unique<ep::MockSkill>(dir / "mock"),
                          ep::ExecutorOptions{true});
    std::string error;
    CHECK(executor.init(&error));

    const auto valid = ep::Json{{"workload", "svc-rpc"},
                                 {"observed_at_ms", ep::now_epoch_ms()},
                                 {"window_ms", 2000},
                                 {"expert", "safe-basic"},
                                 {"foreground", ep::Json{{"p99_latency_ms", 2.0}}},
                                 {"background", ep::Json{{"work_units_per_sec", 800.0},
                                                         {"max_wait_ms", 60.0}}}};

    ep::Json invented = valid;
    invented["requests_per_second"] = 12;
    ep::Json response = executor.handle(ep::Json{{"operation", "service.observe"},
                                                 {"request_id", "req-obs-bad-000001"},
                                                 {"observation", invented}});
    CHECK(response["status"] == "REJECTED");
    CHECK(response["error"]["code"] == "SCHEMA_INVALID");

    // A probe with a wrong clock must not become the system's idea of the present.
    ep::Json future = valid;
    future["observed_at_ms"] = ep::now_epoch_ms() + 600000;
    response = executor.handle(ep::Json{{"operation", "service.observe"},
                                        {"request_id", "req-obs-bad-000002"},
                                        {"observation", future}});
    CHECK(response["status"] == "REJECTED");

    response = executor.handle(ep::Json{{"operation", "service.observe"},
                                        {"request_id", "req-obs-ok-0000001"},
                                        {"observation", valid}});
    CHECK(response["status"] == "OK");
    CHECK(response["accepted"]["workload"] == "svc-rpc");
    // Ingest is advisory: it must not move the generation or write anything.
    const std::uint64_t generation = executor.generation();

    response = executor.handle(ep::Json{{"operation", "policy.status"},
                                        {"request_id", "req-obs-status-0001"}});
    CHECK(response["status"] == "OK");
    CHECK(response["service"]["observed"] == true);
    CHECK(response["service"]["workload"] == "svc-rpc");
    CHECK(response["service"]["bg_work_units_per_sec"] == 800.0);
    CHECK(response["service"]["adverse_experts"].is_object());
    CHECK(response["service"]["outcome_pending"] == false);
    // Lost-observation accounting is the consumer's job: what it accepted and
    // what it turned away must be countable from the control plane, not only from
    // the producer's own log.
    CHECK(response["service"]["observations_accepted"] == 1);
    CHECK(response["service"]["observations_rejected"] == 2);
    CHECK(!response["service"].value("last_rejection", std::string()).empty());
    CHECK(executor.generation() == generation);

    // A window that describes a minute long gone is refused, not silently kept as
    // "the present" -- and the refusal must not displace the fresher measurement.
    ep::Json dead = valid;
    dead["observed_at_ms"] = ep::now_epoch_ms() - 100000;
    response = executor.handle(ep::Json{{"operation", "service.observe"},
                                        {"request_id", "req-obs-dead-000001"},
                                        {"observation", dead}});
    CHECK(response["status"] == "REJECTED");
    CHECK(response["error"]["code"] == "SCHEMA_INVALID");

    // Gaps are only useful if the control plane still reports one after it ends:
    // age_ms exists while someone asks, the high-water and the count outlive it.
    ep::Json brief = valid;
    brief["window_ms"] = 100;
    brief["observed_at_ms"] = ep::now_epoch_ms() - 500;
    response = executor.handle(ep::Json{{"operation", "service.observe"},
                                        {"request_id", "req-obs-brief-000001"},
                                        {"observation", brief}});
    CHECK(response["status"] == "OK");
    response = executor.handle(ep::Json{{"operation", "slo.evaluate"},
                                        {"request_id", "req-obs-slo-000002"},
                                        {"expert", "safe-basic"}});
    CHECK(response["status"] == "OK");
    response = executor.handle(ep::Json{{"operation", "policy.status"},
                                        {"request_id", "req-obs-status-0002"}});
    CHECK(response["service"]["observations_accepted"] == 2);
    CHECK(response["service"]["observations_rejected"] == 3);
    CHECK(response["service"]["window_ms"] == 100);
    CHECK(response["service"]["observation_max_age_ms"].get<std::int64_t>() >= 500);
    CHECK(response["service"]["stale_checks"].get<std::uint64_t>() >= 1);
    CHECK(executor.generation() == generation);

    // An unknown measurement is reported as "not evaluated", never as a pass.
    response = executor.handle(ep::Json{{"operation", "slo.evaluate"},
                                        {"request_id", "req-obs-slo-000001"},
                                        {"expert", "safe-basic"}});
    CHECK(response["service_evidence"] == "present");
  }
}

// The presence check's first branch is the one the integration suite cannot reach:
// `active_` only exists inside the process that received the link FD, so no legal
// sequence leaves a real executor holding an applied policy with link_fd_ < 0.
// Directly constructed, that branch needs no product-side seam -- a fresh SchedSkill
// is exactly the state the branch describes -- and the two modes are distinguishable
// only because name() reports which one is configured.
void test_sched_skill_presence_branches() {
  const auto dir = make_temp_dir("presence");
  ep::SchedPaths paths;
  paths.state_dir = dir;
  paths.sched_ext_dir = dir / "no-such-sched_ext";
  paths.pin_root = dir / "no-such-pins";
  paths.fd_passing = true;

  ep::Applied applied;
  applied.data = ep::Json{{"pid", 4242},
                          {"starttime", 1ULL},
                          {"mode", "class"},
                          {"enable_seq", 7ULL},
                          {"pins", ep::Json::array()}};

  {
    ep::SchedSkill skill(paths);
    CHECK(skill.name() == "sched");
    std::string detail;
    CHECK(!skill.liveness(applied, &detail));
    CHECK(detail == "executor does not hold the struct_ops link fd");
    // verify() shares the same primitive, so it must report the same reason rather
    // than a kernel-state mismatch it never got to look at.
    const ep::VerifyResult verify = skill.verify(applied, 0);
    CHECK(!verify.ok);
    CHECK(verify.detail == "executor does not hold the struct_ops link fd");
    // The mirror guard in verify_rolled_back only fails while the FD is still held,
    // so with none held it moves on to the kernel state it actually checks.
    const ep::VerifyResult rolled = skill.verify_rolled_back(ep::Snapshot{}, 0);
    CHECK(!rolled.ok);
    CHECK(rolled.detail.find("sched_ext state") != std::string::npos);
  }

  paths.fd_passing = false;
  {
    ep::SchedSkill skill(paths);
    CHECK(skill.name() == "sched-ref");
    std::string detail;
    CHECK(!skill.liveness(applied, &detail));
    CHECK(detail == "loader process is gone or was replaced");
  }

  // An applied record that is not sched runtime data at all is refused before any
  // presence reasoning, and says so.
  {
    ep::SchedSkill skill(paths);
    ep::Applied garbage;
    garbage.data = ep::Json{{"unexpected", "shape"}};
    std::string detail;
    CHECK(!skill.liveness(garbage, &detail));
    CHECK(detail == "applied data cannot be parsed as sched runtime");
  }
}

void test_policy_class_declaration() {
  auto with_class = [](const char* role, const char* klass) {
    ep::Json target{{"kind", "pid"},
                    {"pid", 4242},
                    {"role", role},
                    {"role_source", "explicit"}};
    if (klass != nullptr) target["class"] = klass;
    return ep::Json{{"apiVersion", "eulerpilot.io/v1alpha1"},
                    {"kind", "SchedulingPolicy"},
                    {"metadata", ep::Json{{"name", "class-policy"}}},
                    {"spec", ep::Json{{"scope", "scheduler-instance"},
                                      {"mode", "class"},
                                      {"expert", "safe-basic"},
                                      {"source", "human"},
                                      {"targets", ep::Json::array({target})},
                                      {"parameters", valid_policy()["spec"]["parameters"]},
                                      {"safety", valid_policy()["spec"]["safety"]}}}};
  };

  // A declared class survives validation and reaches the binding layer.
  ep::PolicySummary summary;
  std::string error;
  CHECK(ep::validate_scheduling_policy(with_class("FOREGROUND", "LAT"), &summary, &error));
  CHECK(summary.targets.size() == 1);
  CHECK(summary.targets[0].runtime_class == "LAT");
  CHECK(ep::effective_runtime_class("FOREGROUND", "LAT") == "LAT");
  CHECK(ep::effective_runtime_class("NEUTRAL", "") == "");

  // The role default is exactly the previous behavior: only BACKGROUND was classified.
  CHECK(ep::effective_runtime_class("BACKGROUND", "") == "BG");
  CHECK(ep::effective_runtime_class("FOREGROUND", "") == "");
  CHECK(ep::effective_runtime_class("FOREGROUND", "NORMAL") == "NORMAL");

  auto rejected = [](const ep::Json& policy, const char* label) {
    ep::PolicySummary local;
    std::string message;
    if (ep::validate_scheduling_policy(policy, &local, &message)) {
      std::cerr << "expected rejection: " << label << "\n";
      ++g_failures;
    } else if (message.empty()) {
      std::cerr << "rejection without a message: " << label << "\n";
      ++g_failures;
    } else {
      std::cerr << "rejected as expected (" << label << "): " << message << "\n";
    }
  };

  // BG is the contained tier (short slice, vtime penalty). It must never be
  // reachable without the business role that says so, and a BACKGROUND target may
  // not claim a non-contained tier while keeping BG's role: the two would disagree
  // about what to protect.
  rejected(with_class("FOREGROUND", "BG"), "class BG without BACKGROUND role");
  rejected(with_class("NEUTRAL", "BG"), "class BG on a NEUTRAL target");
  rejected(with_class("BACKGROUND", "LAT"), "BACKGROUND claiming a non-BG class");
  rejected(with_class("FOREGROUND", "SUPER"), "unknown class name");

  // Absent class keeps working, including for BACKGROUND.
  CHECK(ep::validate_scheduling_policy(with_class("BACKGROUND", nullptr), &summary, &error));
  CHECK(summary.targets[0].runtime_class.empty());
}

void test_deadline_exceeded_after_apply() {
  const auto dir = make_temp_dir("deadline");
  {
    ep::Executor executor(dir, std::make_unique<SlowMockSkill>(dir / "mock"),
                          ep::ExecutorOptions{true});
    std::string error;
    CHECK(executor.init(&error));
    ep::Json lease = executor.handle(ep::Json{{"operation", "lease.acquire"},
                                              {"request_id", "req-lease-dl"},
                                              {"lease_owner", "unit-owner-01"}});
    CHECK(lease["status"] == "OK");
    const std::uint64_t fence = lease["fence_epoch"].get<std::uint64_t>();
    const ep::Json intent{{"value", 7}};
    ep::Json envelope = make_envelope("policy.apply", intent, "unit-owner-01", fence, 0,
                                      "idem-deadline-0001");
    envelope["deadline"] = ep::iso8601_from_ms(ep::now_epoch_ms() + 100);
    ep::Json response = executor.handle(ep::Json{{"operation", "policy.apply"},
                                                 {"envelope", envelope},
                                                 {"intent", intent}});
    CHECK(response["status"] != "VERIFIED");
    CHECK(response["error"]["code"] == "DEADLINE_EXCEEDED");
    bool exists = true;
    CHECK(read_mock_value(dir, &exists) == 0);
    CHECK(!exists);
    std::cout << "deadline exceeded after apply -> compensated\n";
  }
  std::filesystem::remove_all(dir);
}

}  // namespace

// Protocol §8: a revocation is only proven when the map says the entry is gone, and a
// teardown must still detach when it is not. The fake helper keeps a real per-pid store
// so class-del that silently no-ops is detectable the way bpftool would detect it.
void test_rollback_proves_class_removal() {
  const auto dir = make_temp_dir("rollback-class-readback");
  const auto sched_ext = dir / "sched_ext";
  const auto pin_dir = dir / "pin";
  const auto state_dir = dir / "state";
  const auto class_map = dir / "class-map";
  std::filesystem::create_directories(sched_ext / "root");
  std::filesystem::create_directories(state_dir);
  std::filesystem::create_directories(class_map);
  write_text_file(sched_ext / "state", "disabled\n");
  write_text_file(sched_ext / "root" / "ops", "\n");
  write_text_file(sched_ext / "enable_seq", "0\n");

  const auto loader = dir / "fake-loader.sh";
  write_text_file(loader, R"BASH(#!/usr/bin/env bash
set -e
se="${EP_FAKE_SCHED_EXT:?}"
pin="${EP_FAKE_PIN:?}"
mkdir -p "$se/root" "$pin"
: > "$pin/cfg"
: > "$pin/class_map"
: > "$pin/stats"
echo eulerpilot > "$se/root/ops"
echo $(( $(cat "$se/enable_seq") + 1 )) > "$se/enable_seq"
echo enabled > "$se/state"
trap 'echo disabled > "$se/state"; : > "$se/root/ops"; exit 0' TERM INT
while true; do sleep 0.05; done
)BASH");
  CHECK(::chmod(loader.c_str(), 0755) == 0);
  CHECK(::setenv("EP_FAKE_SCHED_EXT", sched_ext.c_str(), 1) == 0);
  CHECK(::setenv("EP_FAKE_PIN", pin_dir.c_str(), 1) == 0);

  const auto ctrl = dir / "fake-class-ctrl.sh";
  write_text_file(ctrl, R"BASH(#!/usr/bin/env bash
map="${EP_FAKE_CLASS_MAP:?}"
log="${EP_FAKE_CLASS_LOG:?}"
mkdir -p "$map"
cmd="$1"
shift
pid=""
klass=""
while [ $# -gt 0 ]; do
  case "$1" in
    --pid) pid="$2"; shift 2 ;;
    --class) klass="$2"; shift 2 ;;
    *) shift ;;
  esac
done
printf '%s %s\n' "$cmd" "$pid" >> "$log"
case "$cmd" in
  class-set)
    printf '%s' "$klass" > "$map/$pid"
    echo "{\"ok\":true,\"pid\":$pid,\"class\":\"$klass\"}" ;;
  class-del)
    if [ "${EP_FAKE_DEL_NOOPS:-0}" = "1" ]; then
      echo "{\"ok\":true,\"pid\":$pid,\"deleted\":true}"
    else
      rm -f "$map/$pid"
      echo "{\"ok\":true,\"pid\":$pid,\"deleted\":true}"
    fi ;;
  class-get)
    if [ -s "$map/$pid" ]; then
      echo "{\"ok\":true,\"pid\":$pid,\"present\":true,\"class\":\"$(cat "$map/$pid")\"}"
    else
      echo "{\"ok\":true,\"pid\":$pid,\"present\":false}"
    fi ;;
  *) echo '{"ok":true}' ;;
esac
)BASH");
  CHECK(::chmod(ctrl.c_str(), 0755) == 0);
  CHECK(::setenv("EP_FAKE_CLASS_MAP", class_map.c_str(), 1) == 0);

  const ep::Json intent{{"mode", "class"},
                        {"targets", ep::Json::array({ep::Json{
                                             {"kind", "pid"},
                                             {"pid", 4242},
                                             {"role", "BACKGROUND"}}})}};

  const std::string entry = (class_map / "4242").string();
  const std::string log = (dir / "ctrl.log").string();

  // A confirmed deletion: the map itself has to report the entry as gone.
  {
    write_text_file(dir / "ctrl.log", "");
    CHECK(::setenv("EP_FAKE_DEL_NOOPS", "0", 1) == 0);
    CHECK(::setenv("EP_FAKE_CLASS_LOG", log.c_str(), 1) == 0);
    ep::SchedPaths paths;
    paths.state_dir = state_dir / "ok";
    paths.loader = loader.string();
    paths.sched_ext_dir = sched_ext.string();
    paths.pin_dir = pin_dir.string();
    paths.ctrl = ctrl.string();
    ep::Executor executor(dir / "run-ok", std::make_unique<ep::SchedSkill>(paths),
                          ep::ExecutorOptions{true});
    std::string error;
    CHECK(executor.init(&error));
    const ep::Json lease = executor.handle(ep::Json{{"operation", "lease.acquire"},
                                                    {"request_id", "req-lease-c1"},
                                                    {"lease_owner", "unit-owner-01"}});
    CHECK(lease["status"] == "OK");
    const std::uint64_t fence = lease["fence_epoch"].get<std::uint64_t>();
    const ep::Json envelope =
        make_envelope("policy.apply", intent, "unit-owner-01", fence, 0, "idem-class-rb01");
    const ep::Json applied = executor.handle(ep::Json{{"operation", "policy.apply"},
                                                      {"envelope", envelope},
                                                      {"intent", intent}});
    CHECK(applied["status"] == "VERIFIED");
    CHECK(std::filesystem::exists(entry));
    CHECK(read_text_file(entry) == "BG");
    const ep::Json rollback_of{{"apply_action_id", applied["action_id"]},
                               {"original_transaction_id", envelope["transaction_id"]},
                               {"snapshot_id", applied["snapshot_id"]}};
    const ep::Json rollback_envelope =
        make_envelope("policy.rollback", intent, "unit-owner-01", fence, 1,
                      "idem-class-rb01r", rollback_of);
    const ep::Json rolled = executor.handle(ep::Json{{"operation", "policy.rollback"},
                                                     {"envelope", rollback_envelope}});
    CHECK(rolled["status"] == "ROLLED_BACK");
    CHECK(rolled["rollback"]["detail"].get<std::string>().find("unconfirmed") ==
          std::string::npos);
    CHECK(!std::filesystem::exists(entry));
    const std::string calls = read_text_file(log);
    CHECK(calls.find("class-set 4242") != std::string::npos);
    CHECK(calls.find("class-del 4242") != std::string::npos);
    // The read-back is a separate map query after the delete, not a trust in the exit code.
    const auto del_at = calls.find("class-del 4242");
    const auto get_at = calls.find("class-get 4242");
    CHECK(get_at != std::string::npos);
    CHECK(get_at > del_at);
    CHECK(read_text_file(sched_ext / "state") == "disabled");
  }

  // A deletion that no-ops: report it, and still detach -- fail-safe wins over tidy.
  {
    write_text_file(dir / "ctrl.log", "");
    CHECK(::setenv("EP_FAKE_DEL_NOOPS", "1", 1) == 0);
    ep::SchedPaths paths;
    paths.state_dir = state_dir / "stuck";
    paths.loader = loader.string();
    paths.sched_ext_dir = sched_ext.string();
    paths.pin_dir = pin_dir.string();
    paths.ctrl = ctrl.string();
    ep::Executor executor(dir / "run-stuck", std::make_unique<ep::SchedSkill>(paths),
                          ep::ExecutorOptions{true});
    std::string error;
    CHECK(executor.init(&error));
    const ep::Json lease = executor.handle(ep::Json{{"operation", "lease.acquire"},
                                                    {"request_id", "req-lease-c2"},
                                                    {"lease_owner", "unit-owner-01"}});
    const std::uint64_t fence = lease["fence_epoch"].get<std::uint64_t>();
    const ep::Json envelope =
        make_envelope("policy.apply", intent, "unit-owner-01", fence, 0, "idem-class-rb02");
    const ep::Json applied = executor.handle(ep::Json{{"operation", "policy.apply"},
                                                      {"envelope", envelope},
                                                      {"intent", intent}});
    CHECK(applied["status"] == "VERIFIED");
    CHECK(std::filesystem::exists(entry));
    const ep::Json rollback_of{{"apply_action_id", applied["action_id"]},
                               {"original_transaction_id", envelope["transaction_id"]},
                               {"snapshot_id", applied["snapshot_id"]}};
    const ep::Json rollback_envelope =
        make_envelope("policy.rollback", intent, "unit-owner-01", fence, 1,
                      "idem-class-rb02r", rollback_of);
    const ep::Json rolled = executor.handle(ep::Json{{"operation", "policy.rollback"},
                                                     {"envelope", rollback_envelope}});
    CHECK(rolled["status"] == "ROLLED_BACK");
    const std::string detail = rolled["rollback"]["detail"].get<std::string>();
    CHECK(detail.find("class revocation unconfirmed") != std::string::npos);
    CHECK(detail.find("survived deletion") != std::string::npos);
    CHECK(read_text_file(sched_ext / "state") == "disabled");
    CHECK(!executor.degraded());
  }
  std::filesystem::remove_all(dir);
}

// An exiting service must not rely on a fd closing by accident: the effect it put in
// the kernel has to go through the same compensating rollback, with a committed record.
void test_executor_shutdown_releases_active_effect() {
  const auto dir = make_temp_dir("executor-shutdown");
  ep::Executor executor(dir, std::make_unique<ep::MockSkill>(dir / "mock"),
                        ep::ExecutorOptions{true});
  std::string error;
  CHECK(executor.init(&error));
  const ep::Json lease = executor.handle(ep::Json{{"operation", "lease.acquire"},
                                                  {"request_id", "req-lease-sd1"},
                                                  {"lease_owner", "unit-owner-01"}});
  const std::uint64_t fence = lease["fence_epoch"].get<std::uint64_t>();
  const ep::Json intent{{"value", 77}};
  const ep::Json envelope =
      make_envelope("policy.apply", intent, "unit-owner-01", fence, 0, "idem-shutdown-1");
  const ep::Json applied = executor.handle(ep::Json{{"operation", "policy.apply"},
                                                    {"envelope", envelope},
                                                    {"intent", intent}});
  CHECK(applied["status"] == "VERIFIED");
  bool exists = false;
  CHECK(read_mock_value(dir, &exists) == 77);
  CHECK(exists);
  const std::size_t records_before = executor.journal_size();
  CHECK(executor.generation() == 1);

  executor.shutdown("unit test is stopping the executor");
  read_mock_value(dir, &exists);
  CHECK(!exists);
  CHECK(executor.generation() == 2);
  CHECK(executor.journal_size() > records_before);

  // Nothing is active any more, so a second shutdown must not write another record.
  const std::size_t records_after = executor.journal_size();
  executor.shutdown("second shutdown must be a no-op");
  CHECK(executor.journal_size() == records_after);
  std::filesystem::remove_all(dir);
}

// An unwritable state store must not block taking an effect back down to fair, and must
// not quietly let a new effect through either.
void test_lease_survives_unwritable_state_store() {
  const auto dir = make_temp_dir("lease-unwritable-store");
  ep::Executor executor(dir, std::make_unique<ep::MockSkill>(dir / "mock"),
                        ep::ExecutorOptions{true});
  std::string error;
  CHECK(executor.init(&error));
  const ep::Json lease = executor.handle(ep::Json{{"operation", "lease.acquire"},
                                                  {"request_id", "req-lease-us01"},
                                                  {"lease_owner", "unit-owner-01"}});
  CHECK(lease["status"] == "OK");
  CHECK(lease.value("lease_persisted", true));
  const std::uint64_t fence = lease["fence_epoch"].get<std::uint64_t>();

  const ep::Json intent{{"value", 91}};
  const ep::Json envelope =
      make_envelope("policy.apply", intent, "unit-owner-01", fence, 0, "idem-unwrit-0001");
  const ep::Json applied = executor.handle(ep::Json{{"operation", "policy.apply"},
                                                    {"envelope", envelope},
                                                    {"intent", intent}});
  CHECK(applied["status"] == "VERIFIED");
  bool exists = false;
  CHECK(read_mock_value(dir, &exists) == 91);

  // Break only the state file: renaming a regular file onto a directory fails with
  // ENOTDIR for every uid, which is what a full/readonly store looks like to persist().
  std::filesystem::remove(dir / "state.json");
  std::filesystem::create_directory(dir / "state.json");

  const ep::Json second = executor.handle(ep::Json{{"operation", "lease.acquire"},
                                                   {"request_id", "req-lease-us02"},
                                                   {"lease_owner", "unit-owner-01"},
                                                   {"lease_ttl_ms", 30000}});
  CHECK(second["status"] == "OK");
  CHECK(second["lease_persisted"] == false);
  CHECK(!second["warning"].get<std::string>().empty());
  const std::uint64_t second_fence = second["fence_epoch"].get<std::uint64_t>();
  CHECK(second_fence == fence + 1);

  // A *new* effect is still refused while the store is unwritable.
  const ep::Json other{{"value", 92}};
  const ep::Json other_env =
      make_envelope("policy.apply", other, "unit-owner-01", second_fence, 1,
                    "idem-unwrit-0002");
  const ep::Json refused = executor.handle(ep::Json{{"operation", "policy.apply"},
                                                    {"envelope", other_env},
                                                    {"intent", other}});
  CHECK(refused["status"] == "RECOVERY_REQUIRED");
  CHECK(refused["error"]["code"] == "RECOVERY_REQUIRED");

  // Teardown reaches the skill anyway, and says the audit could not be completed.
  const ep::Json rollback_of{{"apply_action_id", applied["action_id"]},
                             {"original_transaction_id", envelope["transaction_id"]},
                             {"snapshot_id", applied["snapshot_id"]}};
  const ep::Json rollback_envelope =
      make_envelope("policy.rollback", intent, "unit-owner-01", second_fence, 1,
                    "idem-unwrit-0003", rollback_of);
  const ep::Json rolled = executor.handle(ep::Json{{"operation", "policy.rollback"},
                                                   {"envelope", rollback_envelope}});
  CHECK(rolled["status"] == "DEGRADED");
  CHECK(rolled["rollback"]["status"] == "EFFECT_RESTORED");
  CHECK(rolled["error"]["message"].get<std::string>().find("state.json") !=
        std::string::npos);
  read_mock_value(dir, &exists);
  CHECK(!exists);

  const ep::Json status = executor.handle(ep::Json{{"operation", "policy.status"},
                                                   {"request_id", "req-status-us01"}});
  CHECK(status["degraded"] == true);
  CHECK(!status["store_warning"].get<std::string>().empty());
  std::filesystem::remove(dir / "state.json");
  std::filesystem::remove_all(dir);
}

int main() {
  test_policy_validation();
  test_target_resolution();
  test_target_binding_lifetime();
  test_refresh_revokes_stale_members();
  test_sha256();
  test_canonical_json();
  test_iso8601();
  test_journal();
  test_journal_fault();
  test_ownership();
  test_executor_apply_and_rollback();
  test_deadline_exceeded_after_apply();
  test_feature_window();
  test_rule_router();
  test_slo_gate();
  test_service_slo();
  test_service_observe_rpc();
  test_autopilot_rules();
  test_executor_external_change();
  test_executor_reconcile_after_crash();
  test_wal_faults();
  test_unsupported_skill();
  test_sched_skill_fake_loader();
  test_sched_skill_presence_branches();
  test_refresh_reclassifies_in_place();
  test_policy_class_declaration();
  test_sched_enable_seq_semantics();
  test_sched_enable_seq_publish_window();
  test_sched_liveness_takeover();
  test_startup_degraded_reason_surfaced();
  test_sched_helper_timeout();
  test_sched_refresh_budget();
  test_sched_eof_budget();
  test_sched_abort_foreign_pin();
  test_rollback_proves_class_removal();
  test_executor_shutdown_releases_active_effect();
  test_lease_survives_unwritable_state_store();
  if (g_failures != 0) {
    std::cerr << g_failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "all unit checks passed\n";
  return 0;
}
