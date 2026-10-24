// SPDX-License-Identifier: GPL-2.0
// EulerPilot v2 measurement probe for scheduler switch downtime and its effect
// on periodic wake-up latency. Two modes, both using CLOCK_MONOTONIC so records
// line up with the kernel's own timestamps:
//
//   sched_switch_probe watch  FILE            # poll sched_ext state ~1ms
//   sched_switch_probe probe  FILE US SECONDS # wake every US, record lateness
//
// watch writes text lines (cheap to eyeball, tiny volume): a record only when
// something changes plus a heartbeat, so a full run stays small enough to keep
// in the repository as raw evidence.
//
// probe writes fixed-size binary records (monotonic ns, lateness ns) because a
// 1ms wake loop over tens of seconds is tens of thousands of samples.
//
// Lateness is measured against an absolute deadline, so it reflects how late the
// thread was actually scheduled, not sleep(3) drift.
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stop_flag;

static void on_stop(int sig)
{
	(void)sig;
	stop_flag = 1;
}

static uint64_t mono_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void read_small(const char *path, char *out, size_t out_sz)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	ssize_t n;

	out[0] = '\0';
	if (fd < 0)
		return;
	n = read(fd, out, out_sz - 1);
	if (n > 0) {
		out[n] = '\0';
		while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == ' '))
			out[--n] = '\0';
	}
	close(fd);
}

static int run_watch(const char *file, int seconds)
{
	FILE *fp = fopen(file, "w");
	char state[64] = "", ops[128] = "", seq[32] = "";
	char pstate[64] = "\0", pops[128] = "\0", pseq[32] = "\0";
	uint64_t start, last_beat = 0;
	int fd;

	if (!fp) {
		perror("watch open");
		return 1;
	}
	fd = fileno(fp);
	fprintf(fp, "# mono_ns\tstate\tops\tenable_seq\n");
	fflush(fp);
	start = mono_ns();
	while (!stop_flag) {
		uint64_t now = mono_ns();

		read_small("/sys/kernel/sched_ext/state", state, sizeof(state));
		read_small("/sys/kernel/sched_ext/root/ops", ops, sizeof(ops));
		read_small("/sys/kernel/sched_ext/enable_seq", seq, sizeof(seq));
		if (strcmp(state, pstate) || strcmp(ops, pops) || strcmp(seq, pseq) ||
		    now - last_beat >= 100000000ULL) {
			char line[288];
			int len = snprintf(line, sizeof(line), "%llu\t%s\t%s\t%s\n",
					   (unsigned long long)now, state, ops, seq);
			if (len > 0)
				fwrite(line, 1, (size_t)len, fp);
			/* Flush immediately: this file is the only clock reference for
			 * the switch boundaries, and losing a buffer on teardown would
			 * void the whole run. */
			fflush(fp);
			last_beat = now;
			strcpy(pstate, state);
			strcpy(pops, ops);
			strcpy(pseq, seq);
		}
		if (seconds > 0 && now - start > (uint64_t)seconds * 1000000000ULL)
			break;
		{
			struct timespec nap = { 0, 1000000 };	/* 1ms */
			nanosleep(&nap, NULL);
		}
	}
	fflush(fp);
	fsync(fd);
	fclose(fp);
	return 0;
}

struct sample {
	uint64_t due_ns;
	uint64_t late_ns;
};

static int run_probe(const char *file, long interval_us, int seconds)
{
	FILE *fp = fopen(file, "w");
	struct sample *samples;
	struct timespec ts;
	uint64_t start, deadline_ns, written = 0;
	long capacity;
	int fd;

	if (!fp) {
		perror("probe open");
		return 1;
	}
	fd = fileno(fp);
	capacity = ((long)seconds + 5) * 1000000L / interval_us;
	samples = calloc((size_t)capacity, sizeof(*samples));
	if (!samples) {
		perror("probe alloc");
		fclose(fp);
		return 1;
	}

	clock_gettime(CLOCK_MONOTONIC, &ts);
	start = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
	deadline_ns = start + 1000000ULL;
	while (!stop_flag) {
		uint64_t now, late;

		deadline_ns += (uint64_t)interval_us * 1000ULL;
		ts.tv_sec = (time_t)(deadline_ns / 1000000000ULL);
		ts.tv_nsec = (long)(deadline_ns % 1000000000ULL);
		while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == EINTR) {
			if (stop_flag)
				goto out;
		}
		now = mono_ns();
		late = now > deadline_ns ? now - deadline_ns : 0;
		if ((uint64_t)written < (uint64_t)capacity) {
			samples[written].due_ns = deadline_ns;
			samples[written].late_ns = late;
			written++;
		}
		if (now - start > (uint64_t)seconds * 1000000000ULL)
			break;
	}
out:
	fwrite(samples, sizeof(*samples), written, fp);
	fflush(fp);
	fsync(fd);
	fclose(fp);
	free(samples);
	fprintf(stderr, "probe_samples=%llu\n", (unsigned long long)written);
	return 0;
}

int main(int argc, char **argv)
{
	signal(SIGINT, on_stop);
	signal(SIGTERM, on_stop);
	if ((argc == 3 || argc == 4) && !strcmp(argv[1], "watch"))
		return run_watch(argv[2], argc == 4 ? atoi(argv[3]) : 120);
	if (argc == 5 && !strcmp(argv[1], "probe"))
		return run_probe(argv[2], atol(argv[3]), atoi(argv[4]));
	fprintf(stderr,
		"usage: %s watch FILE [seconds] | %s probe FILE interval_us seconds\n",
		argv[0], argv[0]);
	return 2;
}
