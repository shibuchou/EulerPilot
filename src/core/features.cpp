#include "ep/features.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace ep {
namespace {

bool read_first_cpu(std::uint64_t* total, std::uint64_t* idle) {
  std::ifstream input("/proc/stat");
  if (!input) return false;
  std::string line;
  if (!std::getline(input, line)) return false;
  std::istringstream stream(line);
  std::string label;
  if (!(stream >> label) || label != "cpu") return false;
  std::uint64_t value = 0;
  std::uint64_t sum = 0;
  std::uint64_t idle_ticks = 0;
  for (int index = 0; index < 10 && (stream >> value); ++index) {
    sum += value;
    if (index == 3 || index == 4) idle_ticks += value;  // idle + iowait
  }
  if (sum == 0) return false;
  *total = sum;
  *idle = idle_ticks;
  return true;
}

bool read_load1(double* load1) {
  std::ifstream input("/proc/loadavg");
  if (!input) return false;
  double value = 0.0;
  if (!(input >> value)) return false;
  *load1 = value;
  return true;
}

bool read_psi(double* avg10) {
  std::ifstream input("/proc/pressure/cpu");
  if (!input) return false;
  std::string line;
  while (std::getline(input, line)) {
    if (line.rfind("some", 0) != 0) continue;
    const std::size_t pos = line.find("avg10=");
    if (pos == std::string::npos) return false;
    const char* begin = line.c_str() + pos + 6;
    char* end = nullptr;
    const double value = std::strtod(begin, &end);
    if (end == begin) return false;
    *avg10 = value;
    return true;
  }
  return false;
}

}  // namespace

template <typename Getter>
MetricWindow FeatureWindow::aggregate(Getter getter, bool Sample::*valid, const char* source,
                                      const char* missing) const {
  MetricWindow metric;
  metric.source = source;
  metric.missing = missing;
  std::int64_t newest = 0;
  double sum = 0.0;
  for (const auto& sample : samples_) {
    if (!(sample.*valid)) continue;
    const double value = getter(sample);
    if (metric.samples == 0) {
      metric.min = value;
      metric.max = value;
    } else {
      if (value < metric.min) metric.min = value;
      if (value > metric.max) metric.max = value;
    }
    metric.valid = true;
    ++metric.samples;
    sum += value;
    if (sample.at_ms > newest) newest = sample.at_ms;
  }
  if (metric.samples > 0) {
    metric.avg = sum / static_cast<double>(metric.samples);
    metric.age_ms = now_epoch_ms() - newest;
    metric.missing.clear();
  }
  return metric;
}

Json MetricWindow::to_json() const {
  Json value{{"valid", valid},
             {"source", source},
             {"samples", samples},
             {"age_ms", age_ms},
             {"min", min},
             {"avg", avg},
             {"max", max}};
  if (!missing.empty()) value["missing"] = missing;
  return value;
}

FeatureWindow::FeatureWindow(std::size_t capacity) : capacity_(capacity == 0 ? 1 : capacity) {}

void FeatureWindow::sample() {
  Sample sample;
  sample.at_ms = now_epoch_ms();
  std::uint64_t total = 0;
  std::uint64_t idle = 0;
  if (read_first_cpu(&total, &idle)) {
    if (has_previous_ && total > previous_total_) {
      const std::uint64_t delta_total = total - previous_total_;
      const std::uint64_t delta_idle = idle >= previous_idle_ ? idle - previous_idle_ : 0;
      const std::uint64_t busy = delta_total > delta_idle ? delta_total - delta_idle : 0;
      sample.cpu_valid = true;
      sample.cpu_busy_pct =
          100.0 * static_cast<double>(busy) / static_cast<double>(delta_total);
      if (sample.cpu_busy_pct < 0.0) sample.cpu_busy_pct = 0.0;
      if (sample.cpu_busy_pct > 100.0) sample.cpu_busy_pct = 100.0;
    }
    previous_total_ = total;
    previous_idle_ = idle;
    has_previous_ = true;
  }
  double load1 = 0.0;
  if (read_load1(&load1)) {
    sample.load_valid = true;
    sample.load1 = load1;
  }
  double psi = 0.0;
  if (read_psi(&psi)) {
    sample.psi_valid = true;
    sample.psi_cpu_some_avg10 = psi;
  }
  samples_.push_back(sample);
  while (samples_.size() > capacity_) samples_.pop_front();
}

MetricWindow FeatureWindow::aggregate_cpu() const {
  return aggregate([](const Sample& sample) { return sample.cpu_busy_pct; },
                   &Sample::cpu_valid, "/proc/stat",
                   "no valid CPU delta in the window yet");
}

MetricWindow FeatureWindow::aggregate_load() const {
  return aggregate([](const Sample& sample) { return sample.load1; }, &Sample::load_valid,
                   "/proc/loadavg", "loadavg unavailable");
}

MetricWindow FeatureWindow::aggregate_psi() const {
  return aggregate([](const Sample& sample) { return sample.psi_cpu_some_avg10; },
                   &Sample::psi_valid, "/proc/pressure/cpu",
                   "PSI CPU pressure unavailable (psi=0 or kernel without PSI)");
}

Json FeatureWindow::snapshot() const {
  const std::int64_t now = now_epoch_ms();
  const std::int64_t window_ms =
      samples_.size() >= 2 ? samples_.back().at_ms - samples_.front().at_ms : 0;
  MetricWindow run_delay;
  run_delay.source = "not sampled";
  run_delay.missing = "host window does not sample run-delay; use BPF/task stats";
  return Json{{"status", "OK"},
              {"window_ms", window_ms},
              {"samples", samples_.size()},
              {"sufficient", samples_.size() >= 3},
              {"sampled_at_ms", samples_.empty() ? 0 : samples_.back().at_ms},
              {"age_ms", samples_.empty() ? 0 : now - samples_.back().at_ms},
              {"metrics",
               Json{{"cpu_busy_pct", aggregate_cpu().to_json()},
                    {"load1", aggregate_load().to_json()},
                    {"psi_cpu_some_avg10", aggregate_psi().to_json()},
                    {"run_delay_ns", run_delay.to_json()}}}};
}

}  // namespace ep
