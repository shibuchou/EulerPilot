#pragma once

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "ep/skill.hpp"

namespace ep {

// Paths are injectable so unit tests can run against a fake sched_ext tree and
// a fake loader. Defaults target the SP3 reference environment.
struct SchedPaths {
  std::filesystem::path state_dir;
  std::string loader = "/usr/local/bin/ep-sched-loader";
  std::string ctrl = "/usr/local/bin/ep-schedctl";
  std::string sched_ext_dir = "/sys/kernel/sched_ext";
  std::string pin_root = "/sys/fs/bpf/eulerpilot-v2";
  // Explicit pin dir; when empty, <pin_root>/<instance_id> is used.
  std::string pin_dir;
  // Upper bound for one ep-schedctl invocation; used by apply/refresh/rollback.
  // Deliberately short: a hung helper must not freeze lease/TTL fallback.
  std::uint64_t ctrl_timeout_ms = 2000;
  // true: the loader attaches once, passes the struct_ops link FD to the
  // executor and exits. false: the loader stays alive as a child process.
  bool fd_passing = false;
};

// P1 sched adapter: drives the v2-built sched_ext loader as a child process.
//
// Ownership rules:
//   - refuse to attach unless sched_ext is disabled and the pin dir is
//     absent/empty (no reuse of someone else's pins);
//   - record pid + /proc starttime + pinned inodes in sched-runtime.json;
//   - on rollback/reconcile only remove pins whose inode still matches;
//   - the child gets PR_SET_PDEATHSIG=SIGTERM so an executor crash detaches the
//     scheduler instead of leaving an orphan running.
//
// One class serves both modes: `sched` hands the link FD over SCM_RIGHTS and the
// loader exits (production path), `sched-ref` keeps a loader child process alive
// as the comparison mode. `name()` reports which one is actually configured, so
// the ready log and policy.status cannot claim the reference mode for an
// FD-passing instance. The loader is GPL-2.0 code derived from SchedPilot and the
// BPF struct_ops name is `eulerpilot`.
class SchedSkill : public Skill {
 public:
  explicit SchedSkill(SchedPaths paths);
  ~SchedSkill() override;

  void configure(const SkillContext& context) override;

  bool refresh(const Applied& applied, const std::vector<TargetBinding>& bindings,
               const RefreshContext& context, std::string* error) override;
  std::string name() const override { return paths_.fd_passing ? "sched" : "sched-ref"; }

  SkillProbe probe() override;
  bool plan(const Json& intent, Plan* out, std::string* error) override;
  bool snapshot(const Plan& plan, Snapshot* out, std::string* error) override;
  bool apply(const Plan& plan, const Snapshot& snapshot, const LeaseState& lease,
             Applied* out, std::string* error) override;
  VerifyResult verify(const Applied& applied, std::int64_t deadline_ms) override;
  bool liveness(const Applied& applied, std::string* detail) override;
  RecoveryResult rollback(const Applied& applied, const Snapshot& snapshot,
                          std::string* error) override;
  VerifyResult verify_rolled_back(const Snapshot& snapshot,
                                  std::int64_t deadline_ms) override;
  RecoveryResult reconcile(const ReconcileContext& context, std::string* error) override;
  RecoveryResult recover_orphan(std::string* error) override;

 private:
  struct PinEntry {
    std::string name;
    std::uint64_t inode = 0;
    std::uint64_t device = 0;
    Json to_json() const;
  };

  // One class_map entry this instance wrote: either a pid target or the cgroup
  // path whose members were expanded into pids. The class is recorded because the
  // refresh path has to re-assert exactly what apply asserted, and a marker that
  // says only "something was classified" cannot be replayed.
  struct Classified {
    std::int64_t pid = 0;   // set for kind=pid
    std::string path;       // set for kind=cgroup (the expansion source)
    std::string klass;      // NORMAL is never written: it means "no entry"
    Json to_json() const;
  };

  struct Runtime {
    std::int64_t pid = 0;
    std::uint64_t starttime = 0;
    std::string mode;
    std::uint64_t enable_seq = 0;
    std::vector<PinEntry> pins;
    std::vector<Classified> classified;
    Json cfg_before;
    Json cfg_after;

    Json to_json() const;
    static bool parse(const Json& value, Runtime* out);
  };

  std::string read_text(const std::string& path) const;
  std::string pin_dir() const;
  std::string read_state() const;
  std::uint64_t read_enable_seq() const;
  std::uint64_t wait_enable_seq_stable(std::uint64_t previous_seq, int timeout_ms) const;
  std::string read_ops() const;
  std::vector<PinEntry> list_pins() const;
  bool preflight(std::string* error) const;
  bool presence_ok(const Runtime& runtime, std::string* detail) const;
  bool write_runtime(const Runtime& runtime, std::string* error) const;
  bool read_runtime(Runtime* runtime) const;
  void remove_runtime() const;
  bool cleanup_pins(const std::vector<PinEntry>& owned, bool* conflict,
                    std::string* error) const;

  std::int64_t spawn_loader(const std::string& mode, const std::string& fd_socket,
                            std::string* error) const;
  bool create_listener(const std::string& path, int* listen_fd, std::string* error) const;
  bool receive_link_fd(int listen_fd, int* link_fd, std::string* error) const;
  bool wait_disabled(int timeout_ms) const;
  bool wait_enabled(std::int64_t pid, std::string* error) const;
  std::vector<PinEntry> wait_pins_settled(int timeout_ms) const;
  bool run_ctrl(const std::vector<std::string>& args, Json* output,
                std::string* error, std::uint64_t timeout_ms = 0) const;
  bool ctrl_get(Json* cfg, std::string* error) const;
  bool ctrl_set(const Json& parameters, Json* cfg, std::string* error) const;
  bool ctrl_restore(const Json& snapshot, Json* cfg, std::string* error) const;
  bool ctrl_verify(const Json& expected, std::string* error) const;
  bool ctrl_class_set(std::int64_t pid, const std::string& klass, std::string* error,
                      std::uint64_t timeout_ms = 0) const;
  bool ctrl_class_del(std::int64_t pid, std::string* error,
                      std::uint64_t timeout_ms = 0) const;
  bool ctrl_class_get(std::int64_t pid, Json* out, std::string* error) const;
  // confirm_readback re-reads the map after each deletion: protocol asks a revocation
  // to be proven by the map, not by the helper's exit status. Refresh leaves it off --
  // a churny cgroup would pay two helper calls per member inside a millisecond budget.
  bool revoke_managed(std::string* error, bool confirm_readback = false);
  std::vector<std::int64_t> cgroup_pids(const std::string& path) const;
  // Best-effort description of effects still readable after a rollback was attempted:
  // cfg values (slice/vtime) and the class entries this instance wrote.
  std::string residual_effects() const;
  bool pid_matches(std::int64_t pid, std::uint64_t starttime) const;
  static std::uint64_t process_starttime(std::int64_t pid);
  static std::string process_cmdline(std::int64_t pid);
  static bool foreign_daemon_running();
  bool stop_pid(std::int64_t pid, std::uint64_t starttime, int timeout_ms) const;

  SchedPaths paths_;
  std::string resolved_pin_dir_;
  int link_fd_ = -1;
  // pid -> class this instance wrote. Revocation deletes by pid, so the class is
  // only needed to re-assert, never to undo.
  std::map<std::int64_t, std::string> managed_class_;
};

}  // namespace ep
