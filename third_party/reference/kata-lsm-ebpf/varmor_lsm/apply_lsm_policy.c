// apply_lsm_policy.c
// Build: gcc -O2 -g -Wall apply_lsm_policy.c varmor_lsm.c -o apply_lsm_policy -lbpf -lelf -lz
//
// Usage:
//   sudo ./apply_lsm_policy /path/to/varmor_lsm.bpf.o <container-host-pid> enforce
//   sudo ./apply_lsm_policy /path/to/varmor_lsm.bpf.o <container-host-pid> complain
//
// Policy:
//   1. deny + audit CAP_SYS_ADMIN
//   2. deny + audit read/write/create/append /etc/shadow
//   3. deny + audit exec /bin/sh
//   4. deny + audit IPv4 outbound connect 0.0.0.0/0
//
// 注意：当前进程需要保持运行，否则 BPF link fd 关闭后 LSM 程序会 detach。

#include "varmor_lsm.h"

#include <errno.h>
#include <linux/capability.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t exiting = 0;

static void on_signal(int sig)
{
    (void)sig;
    exiting = 1;
}

static void die(const char *msg)
{
    perror(msg);
    exit(1);
}

static void check_ret(int ret, const char *what, const char *errbuf)
{
    if (ret != 0) {
        if (errbuf != NULL && errbuf[0] != '\0') {
            fprintf(stderr, "%s failed: %s\n", what, errbuf);
        } else {
            fprintf(stderr, "%s failed: errno=%d (%s)\n", what, errno, strerror(errno));
        }
        exit(1);
    }
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr,
                "Usage:\n"
                "  sudo %s <varmor_lsm.bpf.o> <container-host-pid> <enforce|complain>\n",
                argv[0]);
        return 1;
    }

    const char *bpf_obj_path = argv[1];
    uint32_t pid = (uint32_t)strtoul(argv[2], NULL, 10);
    const char *mode_arg = argv[3];

    uint32_t profile_mode;
    if (strcmp(mode_arg, "enforce") == 0) {
        profile_mode = ENFORCE_MODE;
    } else if (strcmp(mode_arg, "complain") == 0) {
        profile_mode = COMPLAIN_MODE;
    } else {
        fprintf(stderr, "mode must be enforce or complain\n");
        return 1;
    }

    char errbuf[512] = {0};
    uint32_t mnt_ns_id = 0;

    check_ret(varmor_read_mnt_ns_id(pid, &mnt_ns_id, errbuf, sizeof(errbuf)),
              "varmor_read_mnt_ns_id", errbuf);

    printf("[+] target pid=%u mnt_ns_id=%u mode=%s\n", pid, mnt_ns_id, mode_arg);

    VarmorEnforcer *e = varmor_enforcer_new();
    if (e == NULL) {
        die("varmor_enforcer_new");
    }

    if (varmor_enforcer_init(e, bpf_obj_path) != 0) {
        fprintf(stderr, "varmor_enforcer_init failed, bpf_obj_path=%s\n", bpf_obj_path);
        return 1;
    }

    if (varmor_enforcer_start(e) != 0) {
        fprintf(stderr, "varmor_enforcer_start failed\n");
        varmor_enforcer_free(e);
        return 1;
    }

    printf("[+] BPF LSM programs attached\n");

    /*
     * 先清理旧规则，避免同一个 mnt_ns 重复下发时残留。
     */
    varmor_clear_profile_mode(e, mnt_ns_id);
    varmor_clear_capable(e, mnt_ns_id);
    varmor_clear_file(e, mnt_ns_id);
    varmor_clear_bprm(e, mnt_ns_id);
    varmor_clear_network(e, mnt_ns_id);
    varmor_remove_pod_ips(e, mnt_ns_id);
    varmor_clear_ptrace(e, mnt_ns_id);
    varmor_clear_mount(e, mnt_ns_id);

    uint32_t rule_mode = DENY_MODE | AUDIT_MODE;

    /*
     * 1. capability rule: deny CAP_SYS_ADMIN
     */
    CapabilityRule cap_rule;
    memset(&cap_rule, 0, sizeof(cap_rule));

    check_ret(varmor_new_capability_rule(
                  rule_mode,
                  1ULL << CAP_SYS_ADMIN,
                  &cap_rule,
                  errbuf,
                  sizeof(errbuf)),
              "varmor_new_capability_rule", errbuf);

    check_ret(varmor_set_capable(e, mnt_ns_id, &cap_rule),
              "varmor_set_capable", errbuf);

    /*
     * 2. file rule: deny read/write/create/append /etc/shadow
     *
     * 注意：你当前 varmor_set_file() 只支持一次写入一条 file rule。
     * 如果要多个 file rule，需要把 varmor_set_file() 扩展成数组版本。
     */
    PathRule file_rule;
    memset(&file_rule, 0, sizeof(file_rule));

    check_ret(varmor_new_path_rule(
                  rule_mode,
                  "/etc/shadow",
                  AA_MAY_READ | AA_MAY_WRITE | AA_MAY_CREATE | AA_MAY_APPEND,
                  &file_rule,
                  errbuf,
                  sizeof(errbuf)),
              "varmor_new_path_rule(file)", errbuf);

    check_ret(varmor_set_file(e, mnt_ns_id, &file_rule),
              "varmor_set_file", errbuf);

    /*
     * 3. bprm rule: deny exec /bin/sh
     */
    PathRule exec_rule;
    memset(&exec_rule, 0, sizeof(exec_rule));

    check_ret(varmor_new_path_rule(
                  rule_mode,
                  "/bin/sh",
                  AA_MAY_EXEC,
                  &exec_rule,
                  errbuf,
                  sizeof(errbuf)),
              "varmor_new_path_rule(bprm)", errbuf);

    check_ret(varmor_set_bprm(e, mnt_ns_id, &exec_rule),
              "varmor_set_bprm", errbuf);

    /*
     * 4. network rule: deny IPv4 outbound connect 0.0.0.0/0
     *
     * 如果你只想禁某个 IP/端口，比如 1.1.1.1:80，可以改成：
     *   varmor_new_network_connect_rule(rule_mode, NULL, "1.1.1.1", 80, 0, NULL, 0, ...)
     */
    NetworkRule net_rules[1];
    memset(net_rules, 0, sizeof(net_rules));

    check_ret(varmor_new_network_connect_rule(
                  rule_mode,
                  "0.0.0.0/0",
                  NULL,
                  0,
                  0,
                  NULL,
                  0,
                  &net_rules[0],
                  errbuf,
                  sizeof(errbuf)),
              "varmor_new_network_connect_rule", errbuf);

    check_ret(varmor_set_network(e, mnt_ns_id, net_rules, 1),
              "varmor_set_network", errbuf);

    /*
     * 最后再打开 profile mode。
     * BPF 侧只有查到 profile_mode 才会对该 mnt_ns 生效。
     */
    check_ret(varmor_set_profile_mode(e, mnt_ns_id, profile_mode),
              "varmor_set_profile_mode", errbuf);

    printf("[+] policy applied to mnt_ns_id=%u\n", mnt_ns_id);
    printf("[+] keep this process running; press Ctrl+C to clear policy and detach\n");

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    while (!exiting) {
        sleep(1);
    }

    printf("\n[+] clearing policy for mnt_ns_id=%u\n", mnt_ns_id);

    varmor_clear_profile_mode(e, mnt_ns_id);
    varmor_clear_capable(e, mnt_ns_id);
    varmor_clear_file(e, mnt_ns_id);
    varmor_clear_bprm(e, mnt_ns_id);
    varmor_clear_network(e, mnt_ns_id);
    varmor_remove_pod_ips(e, mnt_ns_id);
    varmor_clear_ptrace(e, mnt_ns_id);
    varmor_clear_mount(e, mnt_ns_id);

    varmor_enforcer_free(e);

    printf("[+] done\n");
    return 0;
}