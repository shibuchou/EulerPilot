/* SPDX-License-Identifier: GPL-2.0 */
/* CPU-bound fairness probe: prints iterations, maximum scheduling gap and
 * task runtime from /proc/self/schedstat. Pin it with taskset. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static long long ns_between(const struct timespec *a, const struct timespec *b)
{
	return (long long)(b->tv_sec - a->tv_sec) * 1000000000LL +
	       (b->tv_nsec - a->tv_nsec);
}

int main(int argc, char **argv)
{
	int seconds = argc > 1 ? atoi(argv[1]) : 3;
	struct timespec start, prev, now;
	unsigned long long iters = 0, max_gap = 0, runtime = 0;
	FILE *f;

	clock_gettime(CLOCK_MONOTONIC, &start);
	prev = start;
	for (;;) {
		long long gap;

		clock_gettime(CLOCK_MONOTONIC, &now);
		if (now.tv_sec - start.tv_sec >= seconds)
			break;
		gap = ns_between(&prev, &now);
		if (gap > 0 && (unsigned long long)gap > max_gap)
			max_gap = (unsigned long long)gap;
		prev = now;
		iters++;
	}
	f = fopen("/proc/self/schedstat", "r");
	if (f) {
		if (fscanf(f, "%llu", &runtime) != 1)
			runtime = 0;
		fclose(f);
	}
	printf("{\"pid\":%d,\"iters\":%llu,\"max_gap_ns\":%llu,\"runtime_ns\":%llu}\n",
	       (int)getpid(), iters, max_gap, runtime);
	return 0;
}
