CLANG ?= clang
GCC   ?= gcc

NEW_LIBBPF_DIR := libbpf
NEW_LIBBPF_INC := $(NEW_LIBBPF_DIR)/src
NEW_LIBBPF_LIB := $(NEW_LIBBPF_DIR)/src

BPF_CFLAGS := -O2 -g -Wall -Werror               -I./headers               -D__TARGET_ARCH_x86

USER_CFLAGS := -O2 -g -Wall -Wextra                -I. -I./headers -I$(NEW_LIBBPF_INC)                -I$(NEW_LIBBPF_DIR)/include

USER_LDFLAGS := -L$(NEW_LIBBPF_LIB) -lbpf -lelf -lz

.PHONY: all clean

all: varmor_lsm.bpf.o libvarmor_lsm.a

varmor_lsm.bpf.o: varmor_lsm.bpf.c headers/vmlinux.h
	$(CLANG) $(BPF_CFLAGS) -target bpf -c varmor_lsm.bpf.c -o varmor_lsm.bpf.o

varmor_lsm.o: varmor_lsm.c varmor_lsm.h
	$(GCC) $(USER_CFLAGS) -c varmor_lsm.c -o varmor_lsm.o

libvarmor_lsm.a: varmor_lsm.o
	ar rcs libvarmor_lsm.a varmor_lsm.o

example: libvarmor_lsm.a
	$(GCC) $(USER_CFLAGS) -o example_test example_test.c -L. -lvarmor_lsm $(USER_LDFLAGS)

clean:
	rm -f varmor_lsm.bpf.o varmor_lsm.o libvarmor_lsm.a example_test
