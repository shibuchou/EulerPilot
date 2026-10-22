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

  const auto loader = dir / "fake-loader.sh";
  write_text_file(loader, R"BASH(#!/usr/bin/env bash
set -e
se="${EP_FAKE_SCHED_EXT:?}"
pin="${EP_FAKE_PIN:?}"
mkdir -p "$se/root" "$pin"
: > "$pin/cfg"
: > "$pin/class_map"
: > "$pin/stats"
echo enabled > "$se/state"
echo eulerpilot > "$se/root/ops"
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

  const auto loader = dir / "fake-loader.sh";
  write_text_file(loader, R"BASH(#!/usr/bin/env bash
set -e
se="${EP_FAKE_SCHED_EXT:?}"
pin="${EP_FAKE_PIN:?}"
mkdir -p "$se/root" "$pin"
: > "$pin/cfg"
: > "$pin/class_map"
: > "$pin/stats"
echo enabled > "$se/state"
echo eulerpilot > "$se/root/ops"
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
  applied.data = ep::Json{{"bg_pids", ep::Json::array({1, 2, 3})}};
  const auto start = std::chrono::steady_clock::now();
  ep::RefreshContext context{ep::now_epoch_ms() + 200};
  std::string error;
  CHECK(!skill.refresh(applied, context, &error));
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
  applied.data = ep::Json{{"bg_pids", ep::Json::array({1})}};
  const auto start = std::chrono::steady_clock::now();
  ep::RefreshContext context{ep::now_epoch_ms() + 200};
  std::string error;
  CHECK(!skill.refresh(applied, context, &error));
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
echo enabled > "$se/state"
echo eulerpilot > "$se/root/ops"
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

void test_slo_gate() {
  ep::SloGate gate;
  ep::Json decision = gate.evaluate(
      feature_snapshot(false, false, 0.0, false, 0.0, false, 0.0), "latency-balanced",
      1000);
  CHECK(decision["allowed"] == false);
  CHECK(decision["code"] == "FEATURES_INSUFFICIENT");

  decision = gate.evaluate(feature_snapshot(true, true, 50.0, true, 1.0, true, 30.0),
                           "throughput-balanced", 1000);
  CHECK(decision["allowed"] == false);
  CHECK(decision["code"] == "PSI_PRESSURE_TOO_HIGH");

  decision = gate.evaluate(feature_snapshot(true, true, 50.0, true, 1.0, true, 30.0),
                           "latency-balanced", 1000);
  CHECK(decision["allowed"] == true);

  decision = gate.evaluate(feature_snapshot(true, true, 50.0, true, 1.0, false, 0.0),
                           "latency-balanced", 1000);
  CHECK(decision["allowed"] == false);
  CHECK(decision["code"] == "PSI_FEATURE_MISSING");

  decision = gate.evaluate(feature_snapshot(true, true, 50.0, true, 1.0, false, 0.0),
                           "safe-basic", 1000);
  CHECK(decision["allowed"] == true);

  decision = gate.evaluate(feature_snapshot(true, true, 50.0, true, 1.0, true, 1.0),
                           "cache-local", 1000);
  CHECK(decision["allowed"] == false);
  CHECK(decision["code"] == "LLC_FEATURE_MISSING");

  ep::Json stale = feature_snapshot(true, true, 50.0, true, 1.0, true, 30.0);
  stale["age_ms"] = 5000;
  decision = gate.evaluate(stale, "latency-balanced", 1000);
  CHECK(decision["allowed"] == false);
  CHECK(decision["code"] == "FEATURES_STALE");
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

int main() {
  test_policy_validation();
  test_target_resolution();
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
  test_executor_external_change();
  test_executor_reconcile_after_crash();
  test_wal_faults();
  test_unsupported_skill();
  test_sched_skill_fake_loader();
  test_sched_helper_timeout();
  test_sched_refresh_budget();
  test_sched_eof_budget();
  test_sched_abort_foreign_pin();
  if (g_failures != 0) {
    std::cerr << g_failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "all unit checks passed\n";
  return 0;
}
