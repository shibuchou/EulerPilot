/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ep-schedctl - minimal control tool for the pinned sp_cfg map.
 *
 * This is a GPL-2.0 helper (it includes sched/intf.h and is built from the
 * SchedPilot data plane). EulerPilot v2 keeps it as a separate process so the
 * control-plane binaries do not have to link GPL code.
 *
 *   ep-schedctl get     --pin-dir DIR
 *   ep-schedctl set     --pin-dir DIR --json '{"lat_slice_ns":...}'
 *   ep-schedctl restore --pin-dir DIR --json '<full cfg json from get>'
 */
#include <bpf/bpf.h>

#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#include "intf.h"

namespace {

using Json = nlohmann::json;

constexpr std::uint64_t kMinSliceNs = 250000;
constexpr std::uint64_t kMaxSliceNs = 10000000;
constexpr std::uint64_t kMinPreemptNs = 100000;
constexpr std::uint64_t kMaxPreemptNs = 2000000;
constexpr std::uint64_t kMinBgVtime = 50;
constexpr std::uint64_t kMaxBgVtime = 400;
constexpr std::uint64_t kMinLatBudget = 1;
constexpr std::uint64_t kMaxLatBudget = 95;
constexpr std::uint64_t kMinBudgetWindowMs = 10;
constexpr std::uint64_t kMaxBudgetWindowMs = 10000;
constexpr std::uint64_t kMinAgingMs = 1;
constexpr std::uint64_t kMaxAgingMs = 5000;

int fail(const std::string& message) {
  std::cout << Json{{"ok", false}, {"error", message}}.dump() << "\n";
  return 1;
}

int open_cfg(const std::string& pin_dir, std::string* error) {
  const std::string path = pin_dir + "/cfg";
  const int fd = bpf_obj_get(path.c_str());
  if (fd < 0) {
    *error = "cannot open " + path + ": " + std::strerror(errno);
  }
  return fd;
}

bool read_cfg(int fd, sp_cfg* cfg, std::string* error) {
  const std::uint32_t key = 0;
  if (bpf_map_lookup_elem(fd, &key, cfg) != 0) {
    *error = std::string("bpf_map_lookup_elem failed: ") + std::strerror(errno);
    return false;
  }
  return true;
}

bool write_cfg(int fd, const sp_cfg& cfg, std::string* error) {
  const std::uint32_t key = 0;
  if (bpf_map_update_elem(fd, &key, &cfg, BPF_ANY) != 0) {
    *error = std::string("bpf_map_update_elem failed: ") + std::strerror(errno);
    return false;
  }
  return true;
}

Json cfg_json(const sp_cfg& cfg, const std::string& pin_dir) {
  return Json{{"pin_dir", pin_dir},
              {"intf_version", cfg.intf_version},
              {"generation", cfg.generation},
              {"mode", cfg.mode},
              {"flags", cfg.flags},
              {"heartbeat_seq", cfg.heartbeat_seq},
              {"heartbeat_ns", cfg.heartbeat_ns},
              {"lat_slice_ns", cfg.lat_slice_ns},
              {"comp_slice_ns", cfg.comp_slice_ns},
              {"cache_slice_ns", cfg.cache_slice_ns},
              {"bg_slice_ns", cfg.bg_slice_ns},
              {"preempt_threshold_ns", cfg.preempt_thresh_ns},
              {"migrate_penalty_ns", cfg.migrate_penalty_ns},
              {"lat_vtime_pct", cfg.lat_vtime_pct},
              {"bg_vtime_pct", cfg.bg_vtime_pct},
              {"lat_budget_pct", cfg.lat_budget_pct},
              {"budget_window_ms", cfg.budget_window_ms},
              {"aging_ms", cfg.aging_ms},
              {"llc_affinity", (cfg.flags & SP_FLAG_LLC_AFFINITY) != 0}};
}

bool get_u64(const Json& object, const char* key, std::uint64_t minimum,
             std::uint64_t maximum, std::uint64_t* out, std::string* error) {
  if (!object.contains(key) ||
      (!object.at(key).is_number_integer() && !object.at(key).is_number_unsigned())) {
    *error = std::string(key) + " must be an integer";
    return false;
  }
  const long long value = object.at(key).get<long long>();
  if (value < 0 || static_cast<std::uint64_t>(value) < minimum ||
      static_cast<std::uint64_t>(value) > maximum) {
    *error = std::string(key) + " out of range";
    return false;
  }
  *out = static_cast<std::uint64_t>(value);
  return true;
}

bool get_bool(const Json& object, const char* key, bool* out, std::string* error) {
  if (!object.contains(key) || !object.at(key).is_boolean()) {
    *error = std::string(key) + " must be a boolean";
    return false;
  }
  *out = object.at(key).get<bool>();
  return true;
}

bool params_match(const sp_cfg& left, const sp_cfg& right) {
  return left.mode == right.mode && left.lat_slice_ns == right.lat_slice_ns &&
         left.comp_slice_ns == right.comp_slice_ns &&
         left.cache_slice_ns == right.cache_slice_ns &&
         left.bg_slice_ns == right.bg_slice_ns &&
         left.preempt_thresh_ns == right.preempt_thresh_ns &&
         left.bg_vtime_pct == right.bg_vtime_pct &&
         left.lat_budget_pct == right.lat_budget_pct &&
         left.budget_window_ms == right.budget_window_ms &&
         left.aging_ms == right.aging_ms &&
         ((left.flags ^ right.flags) & SP_FLAG_LLC_AFFINITY) == 0;
}

int cmd_get(const std::string& pin_dir) {
  std::string error;
  const int fd = open_cfg(pin_dir, &error);
  if (fd < 0) return fail(error);
  sp_cfg cfg {};
  const bool ok = read_cfg(fd, &cfg, &error);
  ::close(fd);
  if (!ok) return fail(error);
  std::cout << Json{{"ok", true}, {"cfg", cfg_json(cfg, pin_dir)}}.dump() << "\n";
  return 0;
}

int cmd_set(const std::string& pin_dir, const Json& parameters) {
  std::string error;
  const int fd = open_cfg(pin_dir, &error);
  if (fd < 0) return fail(error);
  sp_cfg cfg {};
  if (!read_cfg(fd, &cfg, &error)) {
    ::close(fd);
    return fail(error);
  }
  sp_cfg updated = cfg;
  std::uint64_t value = 0;
  bool boolean = false;
  int changed = 0;
  if (parameters.contains("lat_slice_ns")) {
    if (!get_u64(parameters, "lat_slice_ns", kMinSliceNs, kMaxSliceNs, &value, &error)) {
      ::close(fd);
      return fail(error);
    }
    updated.lat_slice_ns = value;
    ++changed;
  }
  if (parameters.contains("comp_slice_ns")) {
    if (!get_u64(parameters, "comp_slice_ns", kMinSliceNs, kMaxSliceNs, &value, &error)) {
      ::close(fd);
      return fail(error);
    }
    updated.comp_slice_ns = value;
    ++changed;
  }
  if (parameters.contains("cache_slice_ns")) {
    if (!get_u64(parameters, "cache_slice_ns", kMinSliceNs, kMaxSliceNs, &value, &error)) {
      ::close(fd);
      return fail(error);
    }
    updated.cache_slice_ns = value;
    ++changed;
  }
  if (parameters.contains("bg_slice_ns")) {
    if (!get_u64(parameters, "bg_slice_ns", kMinSliceNs, kMaxSliceNs, &value, &error)) {
      ::close(fd);
      return fail(error);
    }
    updated.bg_slice_ns = value;
    ++changed;
  }
  if (parameters.contains("preempt_threshold_ns")) {
    if (!get_u64(parameters, "preempt_threshold_ns", kMinPreemptNs, kMaxPreemptNs, &value,
                 &error)) {
      ::close(fd);
      return fail(error);
    }
    updated.preempt_thresh_ns = value;
    ++changed;
  }
  if (parameters.contains("bg_vtime_pct")) {
    if (!get_u64(parameters, "bg_vtime_pct", kMinBgVtime, kMaxBgVtime, &value, &error)) {
      ::close(fd);
      return fail(error);
    }
    updated.bg_vtime_pct = static_cast<std::uint32_t>(value);
    ++changed;
  }
  if (parameters.contains("lat_budget_pct")) {
    if (!get_u64(parameters, "lat_budget_pct", kMinLatBudget, kMaxLatBudget, &value,
                 &error)) {
      ::close(fd);
      return fail(error);
    }
    updated.lat_budget_pct = static_cast<std::uint32_t>(value);
    ++changed;
  }
  if (parameters.contains("budget_window_ms")) {
    if (!get_u64(parameters, "budget_window_ms", kMinBudgetWindowMs, kMaxBudgetWindowMs,
                 &value, &error)) {
      ::close(fd);
      return fail(error);
    }
    updated.budget_window_ms = static_cast<std::uint32_t>(value);
    ++changed;
  }
  if (parameters.contains("aging_ms")) {
    if (!get_u64(parameters, "aging_ms", kMinAgingMs, kMaxAgingMs, &value, &error)) {
      ::close(fd);
      return fail(error);
    }
    updated.aging_ms = static_cast<std::uint32_t>(value);
    ++changed;
  }
  if (parameters.contains("llc_affinity")) {
    if (!get_bool(parameters, "llc_affinity", &boolean, &error)) {
      ::close(fd);
      return fail(error);
    }
    if (boolean) {
      updated.flags |= SP_FLAG_LLC_AFFINITY;
    } else {
      updated.flags &= ~SP_FLAG_LLC_AFFINITY;
    }
    ++changed;
  }
  if (changed == 0) {
    ::close(fd);
    return fail("no supported parameter in the request");
  }
  updated.generation = cfg.generation + 1;
  if (!write_cfg(fd, updated, &error)) {
    ::close(fd);
    return fail(error);
  }
  sp_cfg readback {};
  if (!read_cfg(fd, &readback, &error)) {
    ::close(fd);
    return fail(error);
  }
  ::close(fd);
  if (!params_match(updated, readback)) {
    return fail("readback does not match the written cfg");
  }
  std::cout << Json{{"ok", true}, {"cfg", cfg_json(readback, pin_dir)}}.dump() << "\n";
  return 0;
}

int cmd_restore(const std::string& pin_dir, const Json& snapshot) {
  std::string error;
  if (!snapshot.is_object() || !snapshot.contains("cfg")) {
    return fail("restore expects the full output of 'get'");
  }
  const Json& saved = snapshot.at("cfg");
  const int fd = open_cfg(pin_dir, &error);
  if (fd < 0) return fail(error);
  sp_cfg current {};
  if (!read_cfg(fd, &current, &error)) {
    ::close(fd);
    return fail(error);
  }
  sp_cfg restored = current;
  try {
    restored.intf_version = saved.at("intf_version").get<std::uint32_t>();
    restored.mode = saved.at("mode").get<std::uint32_t>();
    restored.flags = saved.at("flags").get<std::uint32_t>();
    restored.lat_slice_ns = saved.at("lat_slice_ns").get<std::uint64_t>();
    restored.comp_slice_ns = saved.at("comp_slice_ns").get<std::uint64_t>();
    restored.cache_slice_ns = saved.at("cache_slice_ns").get<std::uint64_t>();
    restored.bg_slice_ns = saved.at("bg_slice_ns").get<std::uint64_t>();
    restored.preempt_thresh_ns = saved.at("preempt_threshold_ns").get<std::uint64_t>();
    restored.bg_vtime_pct = saved.at("bg_vtime_pct").get<std::uint32_t>();
    restored.lat_budget_pct = saved.at("lat_budget_pct").get<std::uint32_t>();
    restored.budget_window_ms = saved.at("budget_window_ms").get<std::uint32_t>();
    restored.aging_ms = saved.at("aging_ms").get<std::uint32_t>();
  } catch (const std::exception&) {
    ::close(fd);
    return fail("restore snapshot is missing cfg fields");
  }
  if (restored.intf_version != SP_INTF_VERSION) {
    ::close(fd);
    return fail("restore snapshot has a different intf_version");
  }
  restored.generation = current.generation + 1;
  if (!write_cfg(fd, restored, &error)) {
    ::close(fd);
    return fail(error);
  }
  sp_cfg readback {};
  if (!read_cfg(fd, &readback, &error)) {
    ::close(fd);
    return fail(error);
  }
  ::close(fd);
  if (!params_match(restored, readback)) {
    return fail("readback does not match the restored cfg");
  }
  std::cout << Json{{"ok", true}, {"cfg", cfg_json(readback, pin_dir)}}.dump() << "\n";
  return 0;
}

bool parse_class(const std::string& name, std::uint32_t* klass) {
  if (name == "NORMAL") { *klass = SP_CLASS_NORMAL; return true; }
  if (name == "LAT") { *klass = SP_CLASS_LAT; return true; }
  if (name == "COMP") { *klass = SP_CLASS_COMP; return true; }
  if (name == "CACHE") { *klass = SP_CLASS_CACHE; return true; }
  if (name == "BG") { *klass = SP_CLASS_BG; return true; }
  return false;
}

const char* class_name(std::uint32_t klass) {
  switch (klass) {
    case SP_CLASS_LAT: return "LAT";
    case SP_CLASS_COMP: return "COMP";
    case SP_CLASS_CACHE: return "CACHE";
    case SP_CLASS_BG: return "BG";
    default: return "NORMAL";
  }
}

int open_map(const std::string& pin_dir, const char* name, std::string* error) {
  const std::string path = pin_dir + "/" + name;
  const int fd = bpf_obj_get(path.c_str());
  if (fd < 0) *error = "cannot open " + path + ": " + std::strerror(errno);
  return fd;
}

int cmd_class_set(const std::string& pin_dir, std::int64_t pid, const std::string& klass,
                  std::uint32_t confidence) {
  std::uint32_t code = 0;
  if (!parse_class(klass, &code)) return fail("unknown class '" + klass + "'");
  std::string error;
  const int fd = open_map(pin_dir, "class_map", &error);
  if (fd < 0) return fail(error);
  sp_task_class cls {};
  cls.klass = code;
  cls.confidence = confidence > 1000 ? 1000 : confidence;
  cls.source = SP_SRC_EXTERN;
  struct timespec now {};
  clock_gettime(CLOCK_MONOTONIC, &now);
  cls.updated_ns = static_cast<std::uint64_t>(now.tv_sec) * 1000000000ULL +
                   static_cast<std::uint64_t>(now.tv_nsec);
  const std::uint32_t key = static_cast<std::uint32_t>(pid);
  if (bpf_map_update_elem(fd, &key, &cls, BPF_ANY) != 0) {
    ::close(fd);
    return fail(std::string("bpf_map_update_elem failed: ") + std::strerror(errno));
  }
  sp_task_class readback {};
  if (bpf_map_lookup_elem(fd, &key, &readback) != 0 ||
      readback.klass != cls.klass || readback.confidence != cls.confidence) {
    ::close(fd);
    return fail("class_map readback mismatch");
  }
  ::close(fd);
  std::cout << Json{{"ok", true},
                    {"pid", pid},
                    {"class", class_name(readback.klass)},
                    {"confidence", readback.confidence},
                    {"source", readback.source},
                    {"updated_ns", readback.updated_ns}}
                       .dump()
            << "\n";
  return 0;
}

int cmd_class_del(const std::string& pin_dir, std::int64_t pid) {
  std::string error;
  const int fd = open_map(pin_dir, "class_map", &error);
  if (fd < 0) return fail(error);
  const std::uint32_t key = static_cast<std::uint32_t>(pid);
  if (bpf_map_delete_elem(fd, &key) != 0 && errno != ENOENT) {
    ::close(fd);
    return fail(std::string("bpf_map_delete_elem failed: ") + std::strerror(errno));
  }
  ::close(fd);
  std::cout << Json{{"ok", true}, {"pid", pid}, {"deleted", true}}.dump() << "\n";
  return 0;
}

int cmd_class_get(const std::string& pin_dir, std::int64_t pid) {  std::string error;
  const int fd = open_map(pin_dir, "class_map", &error);
  if (fd < 0) return fail(error);
  const std::uint32_t key = static_cast<std::uint32_t>(pid);
  sp_task_class cls {};
  if (bpf_map_lookup_elem(fd, &key, &cls) != 0) {
    ::close(fd);
    std::cout << Json{{"ok", true}, {"pid", pid}, {"present", false}}.dump() << "\n";
    return 0;
  }
  ::close(fd);
  struct timespec now {};
  clock_gettime(CLOCK_MONOTONIC, &now);
  const std::uint64_t now_ns = static_cast<std::uint64_t>(now.tv_sec) * 1000000000ULL +
                               static_cast<std::uint64_t>(now.tv_nsec);
  std::cout << Json{{"ok", true},
                    {"pid", pid},
                    {"present", true},
                    {"class", class_name(cls.klass)},
                    {"confidence", cls.confidence},
                    {"source", cls.source},
                    {"age_ns", now_ns > cls.updated_ns ? now_ns - cls.updated_ns : 0}}
                       .dump()
            << "\n";
  return 0;
}

void usage() {
  std::cerr << "usage: ep-schedctl get|set|restore|class-set|class-get|class-del "
               "--pin-dir DIR [--json JSON] [--pid N] [--class NAME] [--confidence N]\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    usage();
    return 2;
  }
  const std::string command = argv[1];
  std::string pin_dir;
  std::string payload;
  std::string klass;
  std::int64_t pid = 0;
  std::uint32_t confidence = 1000;
  for (int i = 2; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--pin-dir" && i + 1 < argc) {
      pin_dir = argv[++i];
    } else if (arg == "--json" && i + 1 < argc) {
      payload = argv[++i];
    } else if (arg == "--pid" && i + 1 < argc) {
      pid = std::strtoll(argv[++i], nullptr, 10);
    } else if (arg == "--class" && i + 1 < argc) {
      klass = argv[++i];
    } else if (arg == "--confidence" && i + 1 < argc) {
      confidence = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
    } else {
      usage();
      return 2;
    }
  }
  if (pin_dir.empty()) {
    usage();
    return 2;
  }
  if (command == "class-set") {
    if (pid <= 0 || klass.empty()) return fail("class-set requires --pid and --class");
    return cmd_class_set(pin_dir, pid, klass, confidence);
  }
  if (command == "class-get") {
    if (pid <= 0) return fail("class-get requires --pid");
    return cmd_class_get(pin_dir, pid);
  }
  if (command == "class-del") {
    if (pid <= 0) return fail("class-del requires --pid");
    return cmd_class_del(pin_dir, pid);
  }
  if (command == "get") return cmd_get(pin_dir);
  if (command == "set") {
    if (payload.empty()) return fail("set requires --json");
    try {
      return cmd_set(pin_dir, Json::parse(payload));
    } catch (const std::exception& exception) {
      return fail(std::string("invalid --json: ") + exception.what());
    }
  }
  if (command == "restore") {
    if (payload.empty()) return fail("restore requires --json");
    try {
      return cmd_restore(pin_dir, Json::parse(payload));
    } catch (const std::exception& exception) {
      return fail(std::string("invalid --json: ") + exception.what());
    }
  }
  usage();
  return 2;
}
