CXX ?= g++
CC ?= gcc
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2 -g
CLANG ?= clang
BPFTOOL ?= bpftool
CPPFLAGS ?= -Iagent/include
BUILD_DIR := build
AGENT_BIN := $(BUILD_DIR)/eulerpilot-agent
AGENT_SRCS := agent/src/main.cpp agent/src/runtime.cpp agent/src/executors.cpp agent/src/psi_gate.cpp agent/src/skill_registry.cpp agent/src/skill_manager.cpp agent/src/builtin_skills.cpp agent/src/skill_runtime_context.cpp agent/src/metrics_exporter.cpp agent/src/metrics_state.cpp agent/src/capability_detector.cpp agent/src/target_resolver.cpp agent/src/audit_bus.cpp agent/src/action_journal.cpp agent/observer/psi_reader.cpp
AGENT_CPPFLAGS := $(CPPFLAGS) -I./bpf -I$(BUILD_DIR) -I./agent/observer
LIBBPF_CFLAGS := $(shell pkg-config --cflags libbpf 2>/dev/null)
LIBBPF_LIBS := $(shell pkg-config --libs libbpf 2>/dev/null)
YAMLCPP_CFLAGS := $(shell pkg-config --cflags yaml-cpp 2>/dev/null)
YAMLCPP_LIBS := $(shell pkg-config --libs yaml-cpp 2>/dev/null)
VMLINUX := bpf/vmlinux.h
BPF_OBJ := $(BUILD_DIR)/workload_observer.bpf.o
BPF_SKEL := $(BUILD_DIR)/workload_observer.skel.h
OBSERVER_BIN := $(BUILD_DIR)/workload_observer_dump
NETWORK_POLICY_BPF := $(BUILD_DIR)/network_policy_demo.bpf.o
NETWORK_POLICY_SKEL := $(BUILD_DIR)/network_policy_demo.skel.h
SECURITY_POLICY_BPF := $(BUILD_DIR)/security_policy_demo.bpf.o
SECURITY_POLICY_SKEL := $(BUILD_DIR)/security_policy_demo.skel.h

.PHONY: all agent observer network-policy-demo security-policy-demo clean check-env format

all: agent observer

agent: $(AGENT_BIN)

observer: $(OBSERVER_BIN)

network-policy-demo: $(NETWORK_POLICY_BPF) $(NETWORK_POLICY_SKEL)

security-policy-demo: $(SECURITY_POLICY_BPF) $(SECURITY_POLICY_SKEL)

$(AGENT_BIN): $(AGENT_SRCS) bpf/workload_observer.h $(BPF_SKEL) $(VMLINUX) | $(BUILD_DIR)
	$(CXX) $(AGENT_CPPFLAGS) $(CXXFLAGS) $(LIBBPF_CFLAGS) $(YAMLCPP_CFLAGS) $(AGENT_SRCS) -o $@ $(LIBBPF_LIBS) $(YAMLCPP_LIBS) -lelf -lz

$(VMLINUX):
	$(BPFTOOL) btf dump file /sys/kernel/btf/vmlinux format c > $@

$(BPF_OBJ): bpf/workload_observer.bpf.c bpf/workload_observer.h $(VMLINUX) | $(BUILD_DIR)
	$(CLANG) -g -O2 -target bpf -D__TARGET_ARCH_x86 -D__BPF__ -I. -I./bpf -c $< -o $@

$(BPF_SKEL): $(BPF_OBJ)
	$(BPFTOOL) gen skeleton $< > $@

$(OBSERVER_BIN): tools/workload_observer_dump.c bpf/workload_observer.h $(BPF_SKEL) $(VMLINUX) | $(BUILD_DIR)
	$(CC) -Wall -Wextra -O2 -g -I./bpf -I$(BUILD_DIR) $(LIBBPF_CFLAGS) $< -o $@ $(LIBBPF_LIBS) -lelf -lz

$(NETWORK_POLICY_BPF): bpf/network_policy_demo.bpf.c $(VMLINUX) | $(BUILD_DIR)
	$(CLANG) -g -O2 -target bpf -D__TARGET_ARCH_x86 -D__BPF__ -I. -I./bpf -c $< -o $@

$(NETWORK_POLICY_SKEL): $(NETWORK_POLICY_BPF)
	$(BPFTOOL) gen skeleton $< > $@

$(SECURITY_POLICY_BPF): bpf/security_policy_demo.bpf.c $(VMLINUX) | $(BUILD_DIR)
	$(CLANG) -g -O2 -target bpf -D__TARGET_ARCH_x86 -D__BPF__ -I. -I./bpf -c $< -o $@

$(SECURITY_POLICY_SKEL): $(SECURITY_POLICY_BPF)
	$(BPFTOOL) gen skeleton $< > $@

$(BUILD_DIR):
	mkdir -p $@

check-env:
	./scripts/check_env.sh

format:
	@echo "format target reserved for clang-format"

clean:
	rm -rf $(BUILD_DIR) $(VMLINUX)
