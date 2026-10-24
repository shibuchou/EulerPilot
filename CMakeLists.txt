cmake_minimum_required(VERSION 3.20)
project(eulerpilot_v2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Wextra -Wpedantic)

find_package(Threads REQUIRED)

add_library(ep_core STATIC
  src/core/util.cpp
  src/core/domain.cpp
  src/core/policy.cpp
  src/core/cgroup_policy.cpp
  src/core/target.cpp
  src/core/features.cpp
  src/core/router.cpp
  src/core/slo.cpp
  src/core/autopilot.cpp
  src/core/event_guard.cpp
  src/executor/journal.cpp
  src/executor/ownership.cpp
  src/executor/executor.cpp
  src/skills/mock_skill.cpp
  src/skills/sched_skill.cpp
  src/skills/cgroup_skill.cpp
)
target_include_directories(ep_core PUBLIC include third_party)
target_link_libraries(ep_core PUBLIC Threads::Threads)

add_executable(ep-executord src/executor/main.cpp)
target_link_libraries(ep-executord PRIVATE ep_core)

add_executable(ep src/cli/main.cpp)
target_link_libraries(ep PRIVATE ep_core)

# GPL-2.0 sched_ext loader (separate component, see sched/README.md).
find_path(LIBBPF_INCLUDE_DIR bpf/libbpf.h)
find_library(LIBBPF_LIBRARY NAMES bpf)
find_library(ELF_LIBRARY NAMES elf)
find_library(Z_LIBRARY NAMES z)
set(EP_LIBBPF_STATIC "" CACHE FILEPATH
    "Optional static libbpf.a (kernel tree >=1.3); preferred for ep-sched-loader")
if(EP_LIBBPF_STATIC AND EXISTS "${EP_LIBBPF_STATIC}")
  set(EP_LIBBPF_LINK "${EP_LIBBPF_STATIC}")
  message(STATUS "ep-sched-loader: using static libbpf ${EP_LIBBPF_STATIC}")
else()
  set(EP_LIBBPF_LINK "${LIBBPF_LIBRARY}")
endif()
if(LIBBPF_INCLUDE_DIR AND EP_LIBBPF_LINK AND ELF_LIBRARY AND Z_LIBRARY AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sched/scx_schedpilot.bpf.skel.h")
  add_executable(ep-sched-loader sched/loader.c)
  target_include_directories(ep-sched-loader PRIVATE sched ${LIBBPF_INCLUDE_DIR})
  # Upstream scx headers and the generated skeleton are not warning-clean under
  # v2's -Wall -Wextra -Wpedantic policy; keep v2's flags for our own code.
  target_compile_options(ep-sched-loader PRIVATE -w)
  target_link_libraries(ep-sched-loader PRIVATE
    ${EP_LIBBPF_LINK} ${ELF_LIBRARY} ${Z_LIBRARY} Threads::Threads)
  message(STATUS "ep-sched-loader: enabled")
else()
  message(STATUS "ep-sched-loader: disabled (libbpf/elf/z or skeleton missing)")
endif()

# GPL-2.0 cfg-map control helper (separate process; see sched/README.md).
if(LIBBPF_INCLUDE_DIR AND EP_LIBBPF_LINK AND ELF_LIBRARY AND Z_LIBRARY)
  add_executable(ep-schedctl sched/schedctl.cpp)
  target_include_directories(ep-schedctl PRIVATE sched third_party ${LIBBPF_INCLUDE_DIR})
  target_link_libraries(ep-schedctl PRIVATE
    ${EP_LIBBPF_LINK} ${ELF_LIBRARY} ${Z_LIBRARY} Threads::Threads)
  message(STATUS "ep-schedctl: enabled")
else()
  message(STATUS "ep-schedctl: disabled (libbpf/elf/z missing)")
endif()

# Read-only eBPF event pump of the R5 link: loads the events_trace tracing
# object and reports per-cgroup runnable wait; it never writes scheduler state.
if(LIBBPF_INCLUDE_DIR AND EP_LIBBPF_LINK AND ELF_LIBRARY AND Z_LIBRARY AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sched/events_trace.bpf.skel.h")
  add_executable(ep-evtmon src/evtmon/main.cpp)
  target_include_directories(ep-evtmon PRIVATE sched third_party ${LIBBPF_INCLUDE_DIR})
  target_compile_options(ep-evtmon PRIVATE -w)
  target_link_libraries(ep-evtmon PRIVATE
    ${EP_LIBBPF_LINK} ${ELF_LIBRARY} ${Z_LIBRARY} Threads::Threads)
  message(STATUS "ep-evtmon: enabled")
else()
  message(STATUS "ep-evtmon: disabled (libbpf/elf/z or skeleton missing)")
endif()

enable_testing()
add_executable(ep_unit_tests tests/unit/test_main.cpp)
target_link_libraries(ep_unit_tests PRIVATE ep_core)
add_test(NAME unit COMMAND ep_unit_tests)

# The integration test is a shell script; it is registered but only runs when
# explicitly enabled because it starts a real daemon and kills it.
add_test(NAME integration
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration.sh
          $<TARGET_FILE:ep-executord> $<TARGET_FILE:ep>)
set_tests_properties(integration PROPERTIES TIMEOUT 120)

# cgroup v2 integration (kernel-integration layer for the R5 action domain): it starts a
# real daemon, creates a systemd scope and writes cpu.weight through the kernel's own
# interface. Skips (CTest SKIP) without root, cgroup2 or systemd-run.
add_test(NAME integration_cgroup
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration_cgroup.sh
          $<TARGET_FILE:ep-executord> $<TARGET_FILE:ep>)
set_tests_properties(integration_cgroup PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RUN_SERIAL TRUE)

# R5 event link (real kernel + real BPF): tracing -> pump -> event.observe ->
# armed cpu.weight action -> compensation. Needs root, tracepoints and ep-evtmon.
if(TARGET ep-evtmon)
  add_test(NAME integration_events
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration_events.sh
            $<TARGET_FILE:ep-executord> $<TARGET_FILE:ep> $<TARGET_FILE:ep-evtmon>)
  set_tests_properties(integration_events PROPERTIES
    TIMEOUT 240 SKIP_RETURN_CODE 77 RUN_SERIAL TRUE)
endif()

# Real-kernel test; skipped (CTest SKIP) unless a reference loader is provided
# and the environment is idle. Run it explicitly on SP3 with:
#   ctest -R integration_sched -V
set(EP_SCHED_LOADER "" CACHE FILEPATH "SchedPilot reference loader for the real-kernel test")
if(EP_SCHED_LOADER)
  set(EP_SCHED_LOADER_TEST "${EP_SCHED_LOADER}")
elseif(TARGET ep-sched-loader)
  set(EP_SCHED_LOADER_TEST "$<TARGET_FILE:ep-sched-loader>")
else()
  set(EP_SCHED_LOADER_TEST "")
endif()
add_test(NAME integration_sched
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration_sched.sh
          $<TARGET_FILE:ep-executord> $<TARGET_FILE:ep> "${EP_SCHED_LOADER_TEST}")
set_tests_properties(integration_sched PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RUN_SERIAL TRUE)

if(TARGET ep-sched-loader AND TARGET ep-schedctl)
  add_test(NAME integration_fd
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration_fd.sh
            $<TARGET_FILE:ep-executord> $<TARGET_FILE:ep> $<TARGET_FILE:ep-sched-loader>)
  set_tests_properties(integration_fd PROPERTIES
    TIMEOUT 180 SKIP_RETURN_CODE 77 RUN_SERIAL TRUE)

  add_test(NAME integration_lease
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration_lease.sh
            $<TARGET_FILE:ep-executord> $<TARGET_FILE:ep> $<TARGET_FILE:ep-sched-loader>)
  set_tests_properties(integration_lease PROPERTIES
    TIMEOUT 180 SKIP_RETURN_CODE 77 RUN_SERIAL TRUE)

  add_test(NAME integration_ttl
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration_ttl.sh
            $<TARGET_FILE:ep-executord> $<TARGET_FILE:ep> $<TARGET_FILE:ep-sched-loader>)
  set_tests_properties(integration_ttl PROPERTIES
    TIMEOUT 180 SKIP_RETURN_CODE 77 RUN_SERIAL TRUE)
endif()
