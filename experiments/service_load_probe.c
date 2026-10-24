// SPDX-License-Identifier: GPL-2.0
// EulerPilot v2 measurement probe for the served-workload half of the SLO gate.
// It produces the two numbers the policy contract declares but nothing measured
// so far: how late a periodic foreground wake-up actually was, and how much
// background work got done while waiting.
//
//   service_load_probe fg EXPERT WINDOW_S INTERVAL_US  # foreground p99 wake-up lateness
//   service_load_probe bg EXPERT WINDOW_S              # background work units + max wait
//
// One JSON object goes to stdout. The window is measured with CLOCK_MONOTONIC and
// reported with CLOCK_REALTIME so the executor can judge its age.
#define _GNU_SOURCE
#include <errno.h>
#include <math.h>
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

static uint64_t realtime_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);
	return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static int cmp_u64(const void *a, const void *b)
{
	uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;

	return x < y ? -1 : (x > y ? 1 : 0);
}

static int valid_expert(const char *expert)
{
	return !strcmp(expert, "safe-basic") || !strcmp(expert, "latency-balanced") ||
	       !strcmp(expert, "throughput-balanced") || !strcmp(expert, "cache-local");
}

/* Foreground: a hard periodic deadline, and the lateness distribution against it. */
static int run_foreground(const char *expert, int seconds, long interval_us)
{
	struct timespec ts;
	uint64_t *late, start, deadline_ns, written = 0, capacity;
	long i;
	double p99 = 0.0;

	capacity = ((uint64_t)seconds + 5) * 1000000ULL / (uint64_t)interval_us;
	late = calloc((size_t)capacity, sizeof(*late));
	if (!late) {
		perror("foreground alloc");
		return 1;
	}
	clock_gettime(CLOCK_MONOTONIC, &ts);
	start = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
	deadline_ns = start + (uint64_t)interval_us * 1000ULL;
	while (!stop_flag) {
		uint64_t now;

		deadline_ns += (uint64_t)interval_us * 1000ULL;
		ts.tv_sec = (time_t)(deadline_ns / 1000000000ULL);
		ts.tv_nsec = (long)(deadline_ns % 1000000000ULL);
		while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == EINTR) {
			if (stop_flag)
				goto out;
		}
		now = mono_ns();
		late[written++] = now > deadline_ns ? now - deadline_ns : 0;
		if (now - start > (uint64_t)seconds * 1000000000ULL || written >= capacity)
			break;
	}
out:
	if (written == 0) {
		fprintf(stderr, "foreground collected no samples\n");
		free(late);
		return 1;
	}
	qsort(late, (size_t)written, sizeof(*late), cmp_u64);
	i = (long)((double)(written - 1) * 0.99);
	p99 = (double)late[i < 0 ? 0 : (size_t)i] / 1000000.0;
	printf("{\"kind\":\"ServiceObservation\",\"role\":\"foreground\",\"expert\":\"%s\","
	       "\"observed_at_ms\":%llu,\"window_ms\":%llu,\"samples\":%llu,"
	       "\"p99_latency_ms\":%.6f}\n",
	       expert, (unsigned long long)realtime_ms(),
	       (unsigned long long)(mono_ns() - start) / 1000000ULL,
	       (unsigned long long)written, p99);
	free(late);
	return 0;
}

/* Background: how much work landed, and how long a short wait actually took. */
static int run_background(const char *expert, int seconds)
{
	struct timespec ts;
	uint64_t start, iters = 0, max_wait_ns = 0;
	const long want_us = 5000;	/* a 5ms request; the overshoot is the wait */

	clock_gettime(CLOCK_MONOTONIC, &ts);
	start = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
	while (!stop_flag) {
		uint64_t due, now;

		due = mono_ns() + (uint64_t)want_us * 1000ULL;
		ts.tv_sec = (time_t)(due / 1000000000ULL);
		ts.tv_nsec = (long)(due % 1000000000ULL);
		/* TIMER_ABSTIME against an old deadline returns EINTR-free immediately,
		 * so recompute rather than accumulating a drifting deadline. */
		if (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) != 0 &&
		    errno != EINTR)
			break;
		now = mono_ns();
		if (now > due && now - due > max_wait_ns)
			max_wait_ns = now - due;
		/* One work unit is a bounded amount of CPU work between waits. */
		for (volatile long j = 0; j < 200000L; ++j)
			;
		iters++;
		if (now - start > (uint64_t)seconds * 1000000000ULL)
			break;
	}
	{
		uint64_t window_ms = (mono_ns() - start) / 1000000ULL;
		double units = window_ms ? (double)iters * 1000.0 / (double)window_ms : 0.0;

		if (window_ms < 100) {
			fprintf(stderr, "background window too short\n");
			return 1;
		}
		printf("{\"kind\":\"ServiceObservation\",\"role\":\"background\",\"expert\":\"%s\","
		       "\"observed_at_ms\":%llu,\"window_ms\":%llu,\"samples\":%llu,"
		       "\"work_units_per_sec\":%.6f,\"max_wait_ms\":%.6f}\n",
		       expert, (unsigned long long)realtime_ms(),
		       (unsigned long long)window_ms, (unsigned long long)iters, units,
		       (double)max_wait_ns / 1000000.0);
	}
	return 0;
}

int main(int argc, char **argv)
{
	signal(SIGINT, on_stop);
	signal(SIGTERM, on_stop);
	if (argc < 4) {
		fprintf(stderr,
			"usage: %s fg EXPERT SECONDS INTERVAL_US | %s bg EXPERT SECONDS\n",
			argv[0], argv[0]);
		return 2;
	}
	if (!valid_expert(argv[2])) {
		fprintf(stderr, "unknown expert: %s\n", argv[2]);
		return 2;
	}
	if (!strcmp(argv[1], "fg")) {
		if (argc != 5)
			return 2;
		return run_foreground(argv[2], atoi(argv[3]), atol(argv[4]));
	}
	if (!strcmp(argv[1], "bg"))
		return run_background(argv[2], atoi(argv[3]));
	fprintf(stderr, "unknown role: %s\n", argv[1]);
	return 2;
}
