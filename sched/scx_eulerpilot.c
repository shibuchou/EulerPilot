/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <libgen.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <bpf/bpf.h>
#include <scx/common.h>
#include "scx_eulerpilot.bpf.skel.h"

const char help_fmt[] =
"EulerPilot sched_ext scheduler.\n"
"\n"
"Usage: %s [-f] [-v] [--status] [--stats] [--detach]\n"
"\n"
"  -f            Use FIFO scheduling\n"
"  -v            Print libbpf debug messages\n"
"  --status      Print current sched_ext state and exit\n"
"  --stats       Print pinned class_map and sched_ext state summary, then exit\n"
"  --detach      Stop active scx_eulerpilot processes and exit\n"
"  --gate-status Print pinned gate_state_map value and exit\n"
"  --gate-set <normal|active>  Update pinned gate_state_map for wiring tests\n"
"  -h            Display this help and exit\n";

static bool verbose;
static volatile int exit_req;
static bool status_only;
static bool stats_only;
static bool detach_only;
static bool gate_status_only;
static const char *gate_set_value;

struct gate_state_value {
	__u32 state;
	__u32 generation;
	__u64 updated_at_ns;
	__u32 evidence_mask;
	__u32 reserved;
};

static const char *pin_dir = "/sys/fs/bpf/eulerpilot/scx_eulerpilot/v1";

static int libbpf_print_fn(enum libbpf_print_level level, const char *format, va_list args)
{
	if (level == LIBBPF_DEBUG && !verbose)
		return 0;
	return vfprintf(stderr, format, args);
}

static int ensure_pin_dir(void)
{
	if (mkdir("/sys/fs/bpf/eulerpilot", 0755) && errno != EEXIST)
		return -errno;
	if (mkdir("/sys/fs/bpf/eulerpilot/scx_eulerpilot", 0755) && errno != EEXIST)
		return -errno;
	if (mkdir(pin_dir, 0755) && errno != EEXIST)
		return -errno;
	return 0;
}

static void pin_map_alias(int fd, const char *name)
{
	char path[256];

	if (fd < 0 || ensure_pin_dir() != 0)
		return;
	snprintf(path, sizeof(path), "%s/%s", pin_dir, name);
	unlink(path);
	if (bpf_obj_pin(fd, path) != 0 && errno != EEXIST)
		fprintf(stderr, "warn: failed to pin %s: %s\n", path, strerror(errno));
}

static void pin_eulerpilot_maps(struct scx_eulerpilot *skel)
{
	pin_map_alias(bpf_map__fd(skel->maps.class_map), "class_map");
	pin_map_alias(bpf_map__fd(skel->maps.gate_state_map), "gate_state_map");
	pin_map_alias(bpf_map__fd(skel->maps.stats), "stats");
}

static void sigint_handler(int sig)
{
	exit_req = 1;
}

static void print_sched_ext_state(void)
{
	FILE *fp;
	char buf[256];

	fp = fopen("/sys/kernel/sched_ext/state", "r");
	if (fp) {
		if (fgets(buf, sizeof(buf), fp))
			printf("state=%s", buf);
		fclose(fp);
	}
	fp = fopen("/sys/kernel/sched_ext/enable_seq", "r");
	if (fp) {
		if (fgets(buf, sizeof(buf), fp))
			printf("enable_seq=%s", buf);
		fclose(fp);
	}
	fp = fopen("/sys/kernel/sched_ext/nr_rejected", "r");
	if (fp) {
		if (fgets(buf, sizeof(buf), fp))
			printf("nr_rejected=%s", buf);
		fclose(fp);
	}
}

static int open_gate_state_map(void)
{
	int fd;

	fd = bpf_obj_get("/sys/fs/bpf/eulerpilot/scx_eulerpilot/v1/gate_state_map");
	if (fd >= 0)
		return fd;
	return bpf_obj_get("/sys/fs/bpf/gate_state_map");
}

static void print_gate_state(void)
{
	int fd = open_gate_state_map();
	__u32 key = 0;
	struct gate_state_value value = {};

	if (fd < 0) {
		printf("gate_state_map=missing\n");
		return;
	}
	if (bpf_map_lookup_elem(fd, &key, &value) == 0) {
		printf("gate_state=%u generation=%u updated_at_ns=%llu evidence_mask=%u\n",
		       value.state, value.generation,
		       (unsigned long long)value.updated_at_ns, value.evidence_mask);
	}
	close(fd);
}

static void read_stats(struct scx_eulerpilot *skel, __u64 *stats)
{
	int nr_cpus = libbpf_num_possible_cpus();
	__u64 cnts[23][nr_cpus];
	__u32 idx;

	memset(stats, 0, sizeof(stats[0]) * 23);

	for (idx = 0; idx < 23; idx++) {
		int ret, cpu;

		ret = bpf_map_lookup_elem(bpf_map__fd(skel->maps.stats), &idx, cnts[idx]);
		if (ret < 0)
			continue;
		for (cpu = 0; cpu < nr_cpus; cpu++)
			stats[idx] += cnts[idx][cpu];
	}
}

static void print_stats(struct scx_eulerpilot *skel)
{
	__u64 stats[23];

	read_stats(skel, stats);
	printf("class_hits normal=%llu latency=%llu batch=%llu background=%llu ",
	       stats[0], stats[1], stats[2], stats[3]);
	printf("class_map hit=%llu miss=%llu invalid=%llu ",
	       stats[4], stats[5], stats[6]);
	printf("enqueue shared=%llu latency=%llu batch=%llu background=%llu ",
	       stats[7], stats[8], stats[9], stats[10]);
	printf("dispatch shared=%llu latency=%llu batch=%llu background=%llu ",
	       stats[11], stats[12], stats[13], stats[14]);
	printf("running shared=%llu latency=%llu batch=%llu background=%llu ",
	       stats[15], stats[16], stats[17], stats[18]);
	printf("shared_fallback=%llu starvation_guard=%llu bg_wait_total=%llu direct_local_latency=%llu\n",
	       stats[19], stats[20], stats[21], stats[22]);
}

static int detach_running_scheduler(void)
{
	int ret;

	ret = system("pkill -f '/root/olk/kernel-OLK-6.6-atomgit/tools/sched_ext/build/bin/scx_eulerpilot' >/dev/null 2>&1 || true");
	(void)ret;
	print_sched_ext_state();
	return 0;
}

static int set_gate_state(const char *state_name)
{
	int fd = open_gate_state_map();
	__u32 key = 0;
	struct gate_state_value value = {};

	if (fd < 0)
		return 1;
	if (bpf_map_lookup_elem(fd, &key, &value) != 0) {
		close(fd);
		return 1;
	}

	if (strcmp(state_name, "active") == 0)
		value.state = 2;
	else
		value.state = 0;
	value.generation += 1;
	value.updated_at_ns = 0;
	if (bpf_map_update_elem(fd, &key, &value, BPF_ANY) != 0) {
		close(fd);
		return 1;
	}
	close(fd);
	print_gate_state();
	return 0;
}

static void init_gate_state_defaults(void)
{
	int fd = open_gate_state_map();
	__u32 key = 0;
	struct gate_state_value value = {
		.state = 0,
		.generation = 1,
		.updated_at_ns = 0,
		.evidence_mask = 0,
		.reserved = 0,
	};

	if (fd < 0)
		return;
	bpf_map_update_elem(fd, &key, &value, BPF_ANY);
	close(fd);
}

int main(int argc, char **argv)
{
	struct scx_eulerpilot *skel;
	struct bpf_link *link;
	__u32 opt;
	__u64 ecode;
	int filtered_argc = 1;
	char *filtered_argv[argc + 1];
	bool fifo_mode = false;

	libbpf_set_print(libbpf_print_fn);
	signal(SIGINT, sigint_handler);
	signal(SIGTERM, sigint_handler);

restart:
	filtered_argv[0] = argv[0];

	for (int i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--status") == 0) {
			status_only = true;
			continue;
		}
		if (strcmp(argv[i], "--stats") == 0) {
			stats_only = true;
			continue;
		}
		if (strcmp(argv[i], "--detach") == 0) {
			detach_only = true;
			continue;
		}
		if (strcmp(argv[i], "--gate-status") == 0) {
			gate_status_only = true;
			continue;
		}
		if (strcmp(argv[i], "--gate-set") == 0 && i + 1 < argc) {
			gate_set_value = argv[++i];
			continue;
		}
		filtered_argv[filtered_argc++] = argv[i];
	}
	filtered_argv[filtered_argc] = NULL;

	optind = 1;
	while ((opt = getopt(filtered_argc, filtered_argv, "fvh")) != -1) {
		switch (opt) {
		case 'f':
			fifo_mode = true;
			break;
		case 'v':
			verbose = true;
			break;
		default:
			fprintf(stderr, help_fmt, basename(argv[0]));
			return opt != 'h';
		}
	}

	if (detach_only)
		return detach_running_scheduler();

	if (gate_status_only) {
		print_gate_state();
		return 0;
	}

	if (gate_set_value)
		return set_gate_state(gate_set_value);

	if (status_only) {
		print_sched_ext_state();
		return 0;
	}

	skel = SCX_OPS_OPEN(eulerpilot_ops, scx_eulerpilot);
	if (fifo_mode)
		skel->rodata->fifo_sched = true;

	SCX_OPS_LOAD(skel, eulerpilot_ops, scx_eulerpilot, uei);
	link = SCX_OPS_ATTACH(skel, eulerpilot_ops, scx_eulerpilot);
	pin_eulerpilot_maps(skel);
	init_gate_state_defaults();

	if (stats_only) {
		print_sched_ext_state();
		print_stats(skel);
		bpf_link__destroy(link);
		scx_eulerpilot__destroy(skel);
		return 0;
	}

	while (!exit_req && !UEI_EXITED(skel, uei)) {
		print_stats(skel);
		fflush(stdout);
		sleep(1);
	}

	bpf_link__destroy(link);
	ecode = UEI_REPORT(skel, uei);
	scx_eulerpilot__destroy(skel);

	if (UEI_ECODE_RESTART(ecode))
		goto restart;
	return 0;
}
