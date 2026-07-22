CXX ?= g++
CC ?= gcc
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2 -g
CLANG ?= clang
BPFTOOL ?= bpftool
CPPFLAGS ?= -Iagent/include
BUILD_DIR := build
AGENT_BIN := $(BUILD_DIR)/eulerpilot-agent
UNIT_SKILL_REGISTRY_BIN := $(BUILD_DIR)/test_skill_registry
UNIT_RUNTIME_POLICY_BIN := $(BUILD_DIR)/test_runtime_policy
FORMAT_FILES := $(shell find agent bpf sched tests tools -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.c' -o -name '*.h' \) 2>/dev/null)
AGENT_LIB_SRCS := agent/src/runtime.cpp agent/src/executors.cpp agent/src/psi_gate.cpp agent/src/skill_registry.cpp agent/src/skill_manager.cpp agent/src/builtin_skills.cpp agent/src/builtin_skills/resource_control.cpp agent/src/builtin_skills/psi_gate.cpp agent/src/builtin_skills/network_policy.cpp agent/src/builtin_skills/network_qos.cpp agent/src/builtin_skills/network_xdp.cpp agent/src/builtin_skills/security_policy.cpp agent/src/builtin_skills/policy_engine.cpp agent/src/skill_runtime_context.cpp agent/src/metrics_exporter.cpp agent/src/metrics_state.cpp agent/src/capability_detector.cpp agent/src/target_resolver.cpp agent/src/audit_bus.cpp agent/src/action_journal.cpp agent/observer/psi_reader.cpp
AGENT_SRCS := agent/src/main.cpp $(AGENT_LIB_SRCS)
AGENT_CPPFLAGS := $(CPPFLAGS) -I./bpf -I$(BUILD_DIR) -I./agent/observer
LIBBPF_CFLAGS := $(shell pkg-config --cflags libbpf 2>/dev/null)
LIBBPF_LIBS := $(shell pkg-config --libs libbpf 2>/dev/null)
YAMLCPP_CFLAGS := $(shell pkg-config --cflags yaml-cpp 2>/dev/null)
YAMLCPP_LIBS := $(shell pkg-config --libs yaml-cpp 2>/dev/null)
VMLINUX := bpf/vmlinux.h
BPF_OBJ := $(BUILD_DIR)/workload_observer.bpf.o
BPF_SKEL := $(BUILD_DIR)/workload_observer.skel.h
OBSERVER_BIN := $(BUILD_DIR)/workload_observer_dump
NETWORK_POLICY_BPF := $(BUILD_DIR)/network_policy.bpf.o
NETWORK_POLICY_SKEL := $(BUILD_DIR)/network_policy.skel.h
NETWORK_QOS_TC_BPF := $(BUILD_DIR)/network_qos_tc.bpf.o
NETWORK_XDP_BPF := $(BUILD_DIR)/network_xdp.bpf.o
SECURITY_POLICY_BPF := $(BUILD_DIR)/security_policy.bpf.o
SECURITY_POLICY_SKEL := $(BUILD_DIR)/security_policy.skel.h

.PHONY: all agent observer unit-tests network-policy network-policy-demo network-qos-tc network-xdp network-xdp-demo security-policy security-policy-demo clean check-env format format-check

all: agent observer

agent: $(AGENT_BIN)

observer: $(OBSERVER_BIN)

unit-tests: $(UNIT_SKILL_REGISTRY_BIN) $(UNIT_RUNTIME_POLICY_BIN)
	$(UNIT_SKILL_REGISTRY_BIN)
	$(UNIT_RUNTIME_POLICY_BIN)

network-policy: $(NETWORK_POLICY_BPF) $(NETWORK_POLICY_SKEL)

# Compatibility aliases kept for older scripts; canonical targets above use
# the formal Skill names without the historical "-demo" suffix.
network-policy-demo: network-policy

network-qos-tc: $(NETWORK_QOS_TC_BPF)

network-xdp: $(NETWORK_XDP_BPF)

network-xdp-demo: network-xdp

security-policy: $(SECURITY_POLICY_BPF) $(SECURITY_POLICY_SKEL)

security-policy-demo: security-policy

$(AGENT_BIN): $(AGENT_SRCS) bpf/workload_observer.h $(BPF_SKEL) $(VMLINUX) | $(BUILD_DIR)
	$(CXX) $(AGENT_CPPFLAGS) $(CXXFLAGS) $(LIBBPF_CFLAGS) $(YAMLCPP_CFLAGS) $(AGENT_SRCS) -o $@ $(LIBBPF_LIBS) $(YAMLCPP_LIBS) -lelf -lz

$(UNIT_SKILL_REGISTRY_BIN): tests/unit/test_skill_registry.cpp agent/src/skill_registry.cpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

$(UNIT_RUNTIME_POLICY_BIN): tests/unit/test_runtime_policy.cpp $(AGENT_LIB_SRCS) bpf/workload_observer.h $(BPF_SKEL) $(VMLINUX) | $(BUILD_DIR)
	$(CXX) $(AGENT_CPPFLAGS) $(CXXFLAGS) $(LIBBPF_CFLAGS) $(YAMLCPP_CFLAGS) tests/unit/test_runtime_policy.cpp $(AGENT_LIB_SRCS) -o $@ $(LIBBPF_LIBS) $(YAMLCPP_LIBS) -lelf -lz

$(VMLINUX):
	$(BPFTOOL) btf dump file /sys/kernel/btf/vmlinux format c > $@

$(BPF_OBJ): bpf/workload_observer.bpf.c bpf/workload_observer.h $(VMLINUX) | $(BUILD_DIR)
	$(CLANG) -g -O2 -target bpf -D__TARGET_ARCH_x86 -D__BPF__ -I. -I./bpf -c $< -o $@

$(BPF_SKEL): $(BPF_OBJ)
	$(BPFTOOL) gen skeleton $< > $@

$(OBSERVER_BIN): tools/workload_observer_dump.c bpf/workload_observer.h $(BPF_SKEL) $(VMLINUX) | $(BUILD_DIR)
	$(CC) -Wall -Wextra -O2 -g -I./bpf -I$(BUILD_DIR) $(LIBBPF_CFLAGS) $< -o $@ $(LIBBPF_LIBS) -lelf -lz

$(NETWORK_POLICY_BPF): bpf/network_policy.bpf.c $(VMLINUX) | $(BUILD_DIR)
	$(CLANG) -g -O2 -target bpf -D__TARGET_ARCH_x86 -D__BPF__ -I. -I./bpf -c $< -o $@

$(NETWORK_POLICY_SKEL): $(NETWORK_POLICY_BPF)
	$(BPFTOOL) gen skeleton $< > $@

$(NETWORK_QOS_TC_BPF): bpf/network_qos_tc.bpf.c $(VMLINUX) | $(BUILD_DIR)
	$(CLANG) -g -O2 -target bpf -D__TARGET_ARCH_x86 -D__BPF__ -I. -I./bpf -c $< -o $@

$(NETWORK_XDP_BPF): bpf/network_xdp.bpf.c $(VMLINUX) | $(BUILD_DIR)
	$(CLANG) -g -O2 -target bpf -D__TARGET_ARCH_x86 -D__BPF__ -I. -I./bpf -c $< -o $@

$(SECURITY_POLICY_BPF): bpf/security_policy.bpf.c $(VMLINUX) | $(BUILD_DIR)
	$(CLANG) -g -O2 -target bpf -D__TARGET_ARCH_x86 -D__BPF__ -I. -I./bpf -c $< -o $@

$(SECURITY_POLICY_SKEL): $(SECURITY_POLICY_BPF)
	$(BPFTOOL) gen skeleton $< > $@

$(BUILD_DIR):
	mkdir -p $@

check-env:
	./scripts/check_env.sh

format:
	@if ! command -v clang-format >/dev/null 2>&1; then \
		echo "clang-format not found; install clang-format before running make format"; \
		exit 1; \
	fi
	@if [ -z "$(FORMAT_FILES)" ]; then \
		echo "no C/C++ files found for clang-format"; \
	else \
		clang-format -i $(FORMAT_FILES); \
	fi

format-check:
	@if ! command -v clang-format >/dev/null 2>&1; then \
		echo "clang-format not found; install clang-format before running make format-check"; \
		exit 1; \
	fi
	@if [ -z "$(FORMAT_FILES)" ]; then \
		echo "no C/C++ files found for clang-format"; \
	else \
		status=0; \
		for f in $(FORMAT_FILES); do \
			tmp=$$(mktemp); \
			clang-format "$$f" > "$$tmp"; \
			if ! diff -u "$$f" "$$tmp" >/dev/null; then \
				echo "format differs: $$f"; \
				diff -u "$$f" "$$tmp" | sed -n '1,80p'; \
				status=1; \
			fi; \
			rm -f "$$tmp"; \
			if [ "$$status" -ne 0 ]; then exit "$$status"; fi; \
		done; \
	fi

clean:
	rm -rf $(BUILD_DIR) $(VMLINUX)
