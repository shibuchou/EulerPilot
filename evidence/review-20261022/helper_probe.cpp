// Diagnostic only: calls refresh against a locally generated fake helper.
// No real BPF maps, target resolution, loader or sched_ext are used.
#include <chrono>
#include <iostream>
#include <string>
#include "ep/sched_skill.hpp"

int main(int argc, char** argv) {
  if (argc != 5) return 2;
  ep::SchedPaths paths;
  paths.ctrl = argv[1];
  paths.ctrl_timeout_ms = std::stoull(argv[2]);
  paths.pin_dir = argv[4];
  ep::SchedSkill skill(paths);
  ep::Applied applied;
  applied.data = ep::Json{{"bg_pids", ep::Json::array()}};
  const int count = std::stoi(argv[3]);
  for (int i = 0; i < count; ++i) applied.data["bg_pids"].push_back(100000 + i);
  const auto started = std::chrono::steady_clock::now();
  skill.refresh(applied);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started).count();
  std::cout << ep::Json{{"calls", count}, {"configured_timeout_ms", paths.ctrl_timeout_ms},
                        {"elapsed_ms", elapsed}, {"scope", "fake-helper refresh only"}}
                   .dump() << '\n';
}
