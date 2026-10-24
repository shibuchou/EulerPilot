// SPDX-License-Identifier: GPL-2.0
// EulerPilot v2 -- open-loop Redis load generator with per-request records.
//
// Why this exists: the experiment protocol requires goodput@SLO to be counted from
// per-request (or sufficiently precise histogram) records, and forbids deriving it
// from a quantile table. redis-benchmark is closed-loop (it issues the next request
// only after the previous replies), so when the service slows down its offered load
// drops with it -- exactly the coordination miss the protocol warns about. This tool
// instead paces arrivals at a fixed rate and records every completion.
//
//   redis_openloop_probe CONNS RATE SECONDS GET|SET [opts...]
//     --db N          database index (default 0)
//     --keypool N     distinct keys (default 100000)
//     --value-bytes N payload size for SET values and GET key spread (default 64)
//     --slo-ms F      goodput threshold (default 5)
//     --timeout-ms F  per-request deadline; breach closes and resets that connection
//     --raw FILE      append every completion latency as u64 nanoseconds
//     --host H        default 127.0.0.1
//     --port P        default 7777
//
// Output is one JSON line: offered / sent / completed / good / bad / timeouts /
// stalls, achieved rate, and the generator's own CPU time so a client bottleneck
// can be told apart from a server bottleneck. Latency quantiles are deliberately
// left to the analyser, which recomputes them from --raw (reproducibility rule:
// the raw records must be able to rebuild the table).
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

#define MAX_EVENTS 64
#define IO_BUF (1 << 16)
#define PENDING_CAP 4096

static uint64_t now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static uint64_t wall_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);
	return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

struct conn {
	int fd;
	char *inbuf;
	size_t inlen;
	char *outbuf;
	size_t outlen, outpos;
	/* FIFO of issue timestamps; replies arrive in the same order. */
	uint64_t pending[PENDING_CAP];
	int head, count;
	int replies_needed;	/* bytes still needed to finish the current reply */
	int state;		/* 0 = start, 1 = body, 2 = trailing CRLF */
	int dead;
};

enum { REPLY_START = 0, REPLY_BODY = 1, REPLY_CRLF = 2 };

struct config {
	int conns;
	double rate;		/* requests per second, offered */
	int seconds;
	int set_op;		/* 0 GET, 1 SET */
	int db;
	int keypool;
	int value_bytes;
	double slo_ms;
	double timeout_ms;
	char raw[512];
	char host[128];
	int port;
};

static struct config cfg = {
	.conns = 8, .rate = 5000, .seconds = 60, .set_op = 0, .db = 0,
	.keypool = 100000, .value_bytes = 64, .slo_ms = 5.0, .timeout_ms = 100.0,
	.raw = "", .host = "127.0.0.1", .port = 7777,
};

static int encode_request(struct conn *c, uint64_t seq)
{
	char key[64];
	char hdr[160];
	char req[IO_BUF];
	int klen, hlen, n = 0;
	snprintf(key, sizeof(key), "k%u", (unsigned)(seq % (uint64_t)cfg.keypool));
	klen = (int)strlen(key);

	if (cfg.set_op) {
		hlen = snprintf(hdr, sizeof(hdr), "*3\r\n$3\r\nSET\r\n$%d\r\n", klen);
	} else {
		hlen = snprintf(hdr, sizeof(hdr), "*2\r\n$3\r\nGET\r\n$%d\r\n", klen);
	}
	memcpy(req + n, hdr, hlen);
	n += hlen;
	memcpy(req + n, key, klen);
	n += klen;
	req[n++] = '\r';
	req[n++] = '\n';
	if (cfg.set_op) {
		char vhdr[32];

		/* value: fixed width, filled with 'a' */
		hlen = snprintf(vhdr, sizeof(vhdr), "$%d\r\n", cfg.value_bytes);
		memcpy(req + n, vhdr, hlen);
		n += hlen;
		memset(req + n, 'a', cfg.value_bytes);
		n += cfg.value_bytes;
		req[n++] = '\r';
		req[n++] = '\n';
	}

	if (c->outlen + (size_t)n > IO_BUF)
		return 0;	/* backpressure: caller counts a stall */
	memcpy(c->outbuf + c->outlen, req, n);
	c->outlen += (size_t)n;
	return 1;
}

static int conn_connect(struct conn *c)
{
	struct sockaddr_in sa;
	int fd, one = 1;

	fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if (fd < 0)
		return -1;
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons((uint16_t)cfg.port);
	if (inet_pton(AF_INET, cfg.host, &sa.sin_addr) != 1) {
		close(fd);
		return -1;
	}
	if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0 && errno != EINPROGRESS) {
		close(fd);
		return -1;
	}
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
	c->fd = fd;
	c->inlen = 0;
	c->outlen = c->outpos = 0;
	c->head = c->count = 0;
	c->replies_needed = 0;
	c->state = REPLY_START;
	c->dead = 0;
	return 0;
}

/* One completed reply closes exactly one pending request, in FIFO order. */
static void finish_reply(struct conn *c, uint64_t done_now, uint64_t *completed, uint64_t *good,
			 uint64_t *bad, FILE *raw, uint64_t *inflight)
{
	uint64_t lat;

	if (c->count <= 0)
		return;
	lat = done_now > c->pending[c->head] ? done_now - c->pending[c->head] : 0;
	c->head = (c->head + 1) % PENDING_CAP;
	c->count--;
	(*completed)++;
	if (raw)
		fwrite(&lat, sizeof(lat), 1, raw);
	if ((double)lat / 1e6 <= cfg.slo_ms)
		(*good)++;
	else
		(*bad)++;
	if (*inflight > 0)
		(*inflight)--;
}

static int skip_to_crlf(struct conn *c, size_t from, size_t *out)
{
	size_t j = from;

	while (j + 1 < c->inlen) {
		if (c->inbuf[j] == '\r' && c->inbuf[j + 1] == '\n') {
			*out = j + 2;
			return 1;
		}
		j++;
	}
	return 0;
}

/* Consume as many complete replies as possible; each one closes a request. */
static void parse_replies(struct conn *c, uint64_t done_now, uint64_t *completed,
			  uint64_t *good, uint64_t *bad, FILE *raw, uint64_t *inflight)
{
	size_t i = 0;

	for (;;) {
		if (c->state == REPLY_START) {
			size_t next;

			if (i >= c->inlen)
				break;
			switch (c->inbuf[i]) {
			case '$': {
				size_t j = i + 1;
				char tmp[24];
				size_t k = 0;

				while (j < c->inlen && c->inbuf[j] != '\r') {
					if (k + 1 < sizeof(tmp))
						tmp[k++] = c->inbuf[j];
					j++;
				}
				if (j + 1 > c->inlen || !(j + 1 < c->inlen && c->inbuf[j] == '\r' &&
							   c->inbuf[j + 1] == '\n'))
					goto need_more;
				tmp[k] = '\0';
				int bulk;

				i = j + 2;
				bulk = atoi(tmp);
				if (k == 0 || bulk < 0) {	/* $-1: nil reply is complete on its own */
					finish_reply(c, done_now, completed, good, bad, raw, inflight);
					break;
				}
				/* bulk body plus its trailing CRLF, consumed as one unit */
				c->replies_needed = bulk + 2;
				c->state = REPLY_BODY;
				break;
			}
			case '+': case '-': case ':': case '_': case '#': case ',':
				if (!skip_to_crlf(c, i, &next))
					goto need_more;
				i = next;
				finish_reply(c, done_now, completed, good, bad, raw, inflight);
				break;
			case '*': {
				/* Arrays are not expected for GET/SET; skip the header line and
				 * then the element count is consumed by the same logic as replies
				 * arrive, so treat each element line as its own reply terminator. */
				if (!skip_to_crlf(c, i, &next))
					goto need_more;
				i = next;
				finish_reply(c, done_now, completed, good, bad, raw, inflight);
				break;
			}
			default:
				/* Unframed stream: resynchronise by dropping the buffer. */
				c->inlen = 0;
				return;
			}
			continue;
		}
		{
			size_t avail = c->inlen - i;
			size_t take = avail < (size_t)c->replies_needed ? avail : (size_t)c->replies_needed;

			i += take;
			c->replies_needed -= (int)take;
			if (c->replies_needed > 0)
				goto need_more;
			c->state = REPLY_START;
			finish_reply(c, done_now, completed, good, bad, raw, inflight);
		}
	}
need_more:
	if (i > 0) {
		memmove(c->inbuf, c->inbuf + i, c->inlen - i);
		c->inlen -= i;
	}
}

static int flush_out(struct conn *c)
{
	while (c->outpos < c->outlen) {
		ssize_t w = write(c->fd, c->outbuf + c->outpos, c->outlen - c->outpos);

		if (w > 0) {
			c->outpos += (size_t)w;
			continue;
		}
		if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
			return 0;
		return -1;
	}
	c->outlen = c->outpos = 0;
	return 1;
}

static int fill_in(struct conn *c)
{
	for (;;) {
		ssize_t r;

		if (c->inlen + 4096 > IO_BUF)
			break;
		r = read(c->fd, c->inbuf + c->inlen, IO_BUF - c->inlen);
		if (r > 0) {
			c->inlen += (size_t)r;
			continue;
		}
		if (r == 0)
			return -1;
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			break;
		if (errno == EINTR)
			continue;
		return -1;
	}
	return 0;
}

static void usage(void)
{
	fprintf(stderr,
		"usage: redis_openloop_probe CONNS RATE SECONDS GET|SET [--keypool N] [--value-bytes N]\n"
		"                [--slo-ms F] [--timeout-ms F] [--raw FILE] [--host H] [--port P] [--db N]\n");
}

int main(int argc, char **argv)
{
	struct conn *conns;
	struct epoll_event ev, events[MAX_EVENTS];
	int ep, i, raw_fd = -1;
	FILE *raw = NULL;
	uint64_t offered = 0, sent = 0, completed = 0, good = 0, bad = 0;
	uint64_t timeouts = 0, stalls = 0, reconnects = 0, inflight = 0, peak_inflight = 0;
	uint64_t start, deadline, next_send, interval_ns, seq = 0;
	uint64_t wall_start_ms;
	double elapsed = 0;
	struct rusage ru;

	if (argc < 5) {
		usage();
		return 2;
	}
	cfg.conns = atoi(argv[1]);
	cfg.rate = atof(argv[2]);
	cfg.seconds = atoi(argv[3]);
	cfg.set_op = !strcmp(argv[4], "SET");
	if (cfg.conns < 1 || cfg.conns > 512 || cfg.rate <= 0 || cfg.seconds < 1) {
		usage();
		return 2;
	}
	for (i = 5; i < argc; i += 2) {
		const char *opt = argv[i];

		if (i + 1 >= argc) {
			usage();
			return 2;
		}
		if (!strcmp(opt, "--keypool"))
			cfg.keypool = atoi(argv[i + 1]);
		else if (!strcmp(opt, "--value-bytes"))
			cfg.value_bytes = atoi(argv[i + 1]);
		else if (!strcmp(opt, "--slo-ms"))
			cfg.slo_ms = atof(argv[i + 1]);
		else if (!strcmp(opt, "--timeout-ms"))
			cfg.timeout_ms = atof(argv[i + 1]);
		else if (!strcmp(opt, "--db"))
			cfg.db = atoi(argv[i + 1]);
		else if (!strcmp(opt, "--port"))
			cfg.port = atoi(argv[i + 1]);
		else if (!strcmp(opt, "--host"))
			snprintf(cfg.host, sizeof(cfg.host), "%s", argv[i + 1]);
		else if (!strcmp(opt, "--raw")) {
			snprintf(cfg.raw, sizeof(cfg.raw), "%s", argv[i + 1]);
			raw_fd = open(cfg.raw, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
			if (raw_fd < 0) {
				perror("raw open");
				return 1;
			}
		} else {
			fprintf(stderr, "unknown option: %s\n", opt);
			return 2;
		}
	}
	if (cfg.value_bytes < 1 || cfg.value_bytes > 4096) {
		fprintf(stderr, "value bytes out of range\n");
		return 2;
	}
	if (raw_fd >= 0) {
		raw = fdopen(raw_fd, "wb");
		if (!raw) {
			perror("raw fdopen");
			return 1;
		}
	}
	signal(SIGPIPE, SIG_IGN);

	conns = calloc((size_t)cfg.conns, sizeof(*conns));
	if (!conns) {
		perror("alloc");
		return 1;
	}
	ep = epoll_create1(EPOLL_CLOEXEC);
	if (ep < 0) {
		perror("epoll_create1");
		return 1;
	}
	for (i = 0; i < cfg.conns; i++) {
		conns[i].inbuf = malloc(IO_BUF);
		conns[i].outbuf = malloc(IO_BUF);
		if (!conns[i].inbuf || !conns[i].outbuf || conn_connect(&conns[i]) < 0) {
			fprintf(stderr, "connect failed\n");
			return 1;
		}
		ev.events = EPOLLIN;
		ev.data.u32 = (uint32_t)i;
		epoll_ctl(ep, EPOLL_CTL_ADD, conns[i].fd, &ev);
	}

	interval_ns = (uint64_t)(1e9 / cfg.rate);
	start = now_ns();
	/* Wall-clock window boundaries for the summary. They used to be printed as two
	 * calls to wall_ms() on the same line, i.e. always equal, which silently turned
	 * every consumer's "elapsed" into a divide-by-zero (the analyser fell back to the
	 * declared window, which is why no number was wrong -- but the field was). */
	wall_start_ms = wall_ms();
	deadline = start + (uint64_t)cfg.seconds * 1000000000ULL;
	next_send = start;

	while (now_ns() < deadline) {
		uint64_t now = now_ns();
		int nf;

		/* Open loop: arrivals are scheduled, not triggered by replies. */
		while (now >= next_send) {
			struct conn *c = NULL;
			int best = -1;

			offered++;
			for (i = 0; i < cfg.conns; i++) {
				if (conns[i].dead || conns[i].count >= PENDING_CAP - 8)
					continue;
				if (best < 0 || conns[i].count < conns[best].count) {
					best = i;
					c = &conns[i];
				}
			}
			if (!c) {
				stalls++;
			} else if (!encode_request(c, seq++)) {
				stalls++;
			} else {
				c->pending[(c->head + c->count) % PENDING_CAP] = now;
				c->count++;
				sent++;
				inflight++;
				if (inflight > peak_inflight)
					peak_inflight = inflight;
				/* Push now: waiting for the next flush pass adds a full
				 * epoll_wait timeout to every request whenever arrivals are sparse. */
				if (flush_out(c) < 0)
					c->dead = 1;
			}
			next_send += interval_ns;
			if (next_send < now - (uint64_t)cfg.timeout_ms * 1000000ULL)
				next_send = now;	/* do not try to catch up a whole backlog */
		}

		nf = epoll_wait(ep, events, MAX_EVENTS, 5);
		for (i = 0; i < nf; i++) {
			struct conn *c = &conns[events[i].data.u32];
			uint64_t t;

			if (events[i].events & (EPOLLERR | EPOLLHUP)) {
				c->dead = 1;
				continue;
			}
			if (events[i].events & EPOLLIN) {
				if (fill_in(c) < 0) {
					c->dead = 1;
					continue;
				}
				t = now_ns();
				parse_replies(c, t, &completed, &good, &bad, raw, &inflight);
			}
			/* Output is pushed in a pass over all connections below: waiting for an
			 * EPOLLOUT notification would need an edge that level-triggered epoll
			 * reports continuously, and asking for it makes this loop spin. */
		}

		for (i = 0; i < cfg.conns; i++) {
			if (conns[i].dead || conns[i].outpos >= conns[i].outlen)
				continue;
			if (flush_out(&conns[i]) < 0)
				conns[i].dead = 1;
		}

		/* Timeouts and reconnects: a dead connection drops its pending window. */
		for (i = 0; i < cfg.conns; i++) {
			struct conn *c = &conns[i];

			now = now_ns();
			if (!c->dead && c->count > 0) {
				double age = (double)(now - c->pending[c->head]) / 1e6;

				if (age > cfg.timeout_ms) {
					timeouts += (uint64_t)c->count;
					if (inflight >= (uint64_t)c->count)
						inflight -= (uint64_t)c->count;
					c->head = c->count = 0;
					c->dead = 1;
				}
			}
			if (c->dead) {
				epoll_ctl(ep, EPOLL_CTL_DEL, c->fd, NULL);
				close(c->fd);
				reconnects++;
				if (conn_connect(c) < 0) {
					fprintf(stderr, "reconnect failed\n");
					return 1;
				}
				ev.events = EPOLLIN;
				ev.data.u32 = (uint32_t)i;
				epoll_ctl(ep, EPOLL_CTL_ADD, c->fd, &ev);
			}
		}
	}

	/* Drain what is already in flight so late replies are not lost. */
	for (i = 0; i < cfg.conns; i++) {
		struct conn *c = &conns[i];
		int guard = 0;

		while (c->count > 0 && guard++ < 200) {
			uint64_t t = now_ns();

			if (t > deadline + 2000000000ULL)
				break;
			if (fill_in(c) < 0)
				break;
			parse_replies(c, now_ns(), &completed, &good, &bad, raw, &inflight);
			if (c->count)
				usleep(1000);
		}
	}
	elapsed = (double)(now_ns() - start) / 1e9;
	getrusage(RUSAGE_SELF, &ru);
#define TV2D(tv) ((double)(tv).tv_sec + (double)(tv).tv_usec / 1e6)
	{
		double cpu = TV2D(ru.ru_utime) + TV2D(ru.ru_stime);
		uint64_t lost = sent > completed ? sent - completed : 0;

		printf("{\"kind\":\"RedisOpenLoop\",\"host\":\"%s\",\"port\":%d,\"op\":\"%s\","
		       "\"db\":%d,\"conns\":%d,\"keypool\":%d,\"value_bytes\":%d,"
		       "\"offered_rate_rps\":%.2f,\"slo_ms\":%.3f,\"timeout_ms\":%.1f,"
		       "\"elapsed_s\":%.3f,\"offered\":%llu,\"sent\":%llu,\"completed\":%llu,"
		       "\"good\":%llu,\"bad\":%llu,\"timeouts\":%llu,\"stalls\":%llu,"
		       "\"reconnects\":%llu,\"unanswered\":%llu,\"peak_inflight\":%llu,"
		       "\"achieved_rps\":%.2f,\"goodput_at_slo_rps\":%.2f,"
			       "\"deficit_rps\":%.2f,\"cpu_s\":%.3f,\"cpu_per_second\":%.3f,"
		       "\"offered_at_ms\":%llu,\"finished_at_ms\":%llu}\n",
		       cfg.host, cfg.port, cfg.set_op ? "SET" : "GET", cfg.db, cfg.conns,
		       cfg.keypool, cfg.value_bytes, cfg.rate, cfg.slo_ms, cfg.timeout_ms,
		       elapsed, (unsigned long long)offered, (unsigned long long)sent,
		       (unsigned long long)completed, (unsigned long long)good,
		       (unsigned long long)bad, (unsigned long long)timeouts,
		       (unsigned long long)stalls, (unsigned long long)reconnects,
		       (unsigned long long)lost, (unsigned long long)peak_inflight,
		       elapsed ? (double)completed / elapsed : 0.0,
		       elapsed ? (double)good / elapsed : 0.0,
		       elapsed ? (double)(offered - completed) / elapsed : 0.0,
		       cpu, elapsed ? cpu / elapsed : 0.0,
		       (unsigned long long)wall_start_ms, (unsigned long long)wall_ms());
	}
#undef TV2D
	if (raw)
		fclose(raw);
	return 0;
}
