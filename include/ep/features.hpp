#pragma once

#include <cstdint>
#include <deque>
#include <string>

#include "ep/util.hpp"

namespace ep {

// One metric over the current window. `valid=false` carries the reason in
// `missing`; callers must degrade conservatively instead of treating 0 as data.
struct MetricWindow {
  bool valid = false;
  std::string source;
  std::size_t samples = 0;
  std::int64_t age_ms = 0;
  double min = 0.0;
  double avg = 0.0;
  double max = 0.0;
  std::string missing;

  Json to_json() const;
};

// Host-level rolling feature window. The first CPU sample has no delta and is
// marked invalid; PSI is invalid when /proc/pressure/cpu is unavailable.
class FeatureWindow {
 public:
  explicit FeatureWindow(std::size_t capacity = 30);

  void sample();
  Json snapshot() const;
  std::size_t size() const { return samples_.size(); }

 private:
  struct Sample {
    std::int64_t at_ms = 0;
    bool cpu_valid = false;
    double cpu_busy_pct = 0.0;
    bool load_valid = false;
    double load1 = 0.0;
    bool psi_valid = false;
    double psi_cpu_some_avg10 = 0.0;
  };

  template <typename Getter>
  MetricWindow aggregate(Getter getter, bool Sample::*valid, const char* source,
                         const char* missing) const;

  MetricWindow aggregate_cpu() const;
  MetricWindow aggregate_load() const;
  MetricWindow aggregate_psi() const;

  std::size_t capacity_;
  std::deque<Sample> samples_;
  std::uint64_t previous_total_ = 0;
  std::uint64_t previous_idle_ = 0;
  bool has_previous_ = false;
};

}  // namespace ep
