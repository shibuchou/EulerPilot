#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stop_requested = 0;

static void on_signal(int sig) {
    (void)sig;
    stop_requested = 1;
}

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

int main(int argc, char **argv) {
    const double duration = argc > 1 ? atof(argv[1]) : 8.0;
    const double end = now_s() + duration;
    volatile uint64_t state = (uint64_t)getpid() * 11400714819323198485ull;
    uint64_t ops = 0;

    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);

    while (!stop_requested && now_s() < end) {
        for (int i = 0; i < 4096; ++i) {
            state ^= state << 7;
            state ^= state >> 9;
            state *= 1099511628211ull;
        }
        ops += 4096;
    }

    const double elapsed = duration > 0.0 ? duration : 1.0;
    printf("pid=%d ops=%" PRIu64 " elapsed_s=%.6f state=%" PRIu64 "\n",
           getpid(), ops, elapsed, state);
    return errno == 0 ? 0 : 1;
}
