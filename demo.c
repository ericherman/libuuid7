/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2024 Eric Herman <eric@freesa.org> */

#include "uuid7.h"

#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

char *uuid7_decode(char *buf, size_t buflen, const uint8_t *bytes)
{
	union uuid7 tmp;
	memcpy(tmp.bytes, bytes, 16);
	uint32_t nanos = (((uint32_t)tmp.hifrac) << 12) | tmp.lofrac;

	const char *fmt =
	    "%" PRIu64 ".%" PRIu32 " [%" PRIu16 "] (%" PRIu8 ",%" PRIu8
	    ") %012" PRIx64;

	snprintf(buf, buflen, fmt, tmp.seconds, nanos, tmp.sequence,
		 tmp.uuid_ver, tmp.uuid_var, tmp.rand);

	if ((tmp.uuid_ver != uuid7_version)
	    || (tmp.uuid_var != uuid7_variant)) {
		return NULL;
	}
	return buf;
}

#include <stdio.h>

void die(const char *file, long line, const char *func, int err, char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "%s:%ld %s(): ", file, line, func);
	if (err) {
		fprintf(stderr, "%s: ", strerror(err));
	}
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	exit(EXIT_FAILURE);
}

#define Die(...) \
	die(__FILE__, __LINE__, __func__, errno, __VA_ARGS__);

static clockid_t fd_to_clockid(unsigned int fd)
{
	const unsigned long clockfd = 3;
	return ((~(clockid_t) (fd) << 3) | clockfd);
}

static int clockid_to_fd(clockid_t clk)
{
	return ((unsigned int)~((clk) >> 3));
}

extern const clockid_t uuid7_clockid;

static long double elapsed_ts(struct timespec from, struct timespec to) {
	long double from_seconds = from.tv_sec + (from.tv_nsec / 1000000000.0);
	long double to_seconds = to.tv_sec + (to.tv_nsec / 1000000000.0);
	long double elapsed = (to_seconds - from_seconds);
	return elapsed;
}

#include <fcntl.h>
int main(int argc, char **argv)
{
	clockid_t clockid;
	if (argc > 1) {
		// e.g.: "/dev/ptp0"
		const char *dev_clock = argv[1];
		int fd = open(dev_clock, O_RDWR);
		if (fd < 0) {
			Die("open(\"%s\", O_RDWR)", dev_clock);
		}
		clockid = fd_to_clockid(fd);
	} else {
		clockid = uuid7_clockid;
	}
	int clockfd = clockid_to_fd(clockid);
	if (clockfd >= 0) {
		printf("clockid fd: %d\n", clockid_to_fd(clockid));
	}

	const size_t ts_len = 2 * 1000 * 1000;
	size_t ts_size = sizeof(struct timespec) * ts_len;
	struct timespec *ts = (struct timespec *)malloc(ts_size);
	if (!ts) {
		Die("failed to allocate %zu bytes?", ts_size);
	}
	printf("Checking the clock ...");
	if (clock_getres(clockid, &ts[0])) {
		Die("clock_getres(%ld, &ts)", (long)clockid);
	}
	printf(" done.\n");

	printf("    resolution:  %jd.%09ld\n", (intmax_t) ts[0].tv_sec,
	       ts[0].tv_nsec);

	printf("Calling clock_gettime in a tight loop %zu times ...", ts_len);
	struct timespec ts_begin;
	struct timespec ts_final;

	clock_gettime(clockid, &ts_begin);
	for (size_t i = 0; i < ts_len; ++i) {
		if (clock_gettime(clockid, &ts[i])) {
			Die("clock_gettime(%ld, &ts)", (long)clockid);
		}
	}
	clock_gettime(clockid, &ts_final);
	long double elapsed = elapsed_ts(ts_begin, ts_final);
	long double percall = (elapsed / ts_len);
	printf(" done in %.9LF seconds (~%.9LF per).\n", elapsed, percall);

	size_t duplicates = 0;
	for (size_t i = 1; i < ts_len; ++i) {
		if ((ts[i - 1].tv_sec == ts[i].tv_sec)
		    && (ts[i - 1].tv_nsec == ts[i].tv_nsec)) {
			++duplicates;
		}
	}
	printf("\tfor %zu calls to clock_gettime, %zu duplicates were found\n",
	       ts_len, duplicates);
	if (duplicates) {
		printf("\t(sequence may not always be zero)\n");
	} else {
		printf("\t(sequence will probably always be zero)\n");
	}

	size_t subset = 10;
	printf("First %zu results:\n", subset);
	for (size_t i = 0; i < subset; ++i) {
		printf("\t%10jd.%09ld\n", (intmax_t) ts[i].tv_sec,
		       ts[i].tv_nsec);
	}

	size_t uuids_len = 100;
	uint8_t uuid7s[100][16];
	memset(uuid7s, 0x00, uuids_len * 16);

	printf("\n\nGenerating %zu UUIDs ...", uuids_len);
#ifndef UUID7_SKIP_MUTEX
	uuid7_mutex_init();
#endif

	clock_gettime(clockid, &ts_begin);
	for (size_t i = 0; i < 5; ++i) {
		uuid7(uuid7s[i]);
	}
	clock_gettime(clockid, &ts_final);
	long double elapsed1 = elapsed_ts(ts_begin, ts_final);

	struct timespec snooze = { 0, 100 };
	nanosleep(&snooze, NULL);

	clock_gettime(clockid, &ts_begin);
	for (size_t i = 5; i < uuids_len; ++i) {
		uuid7(uuid7s[i]);
	}
	clock_gettime(clockid, &ts_final);
	long double elapsed2 = elapsed_ts(ts_begin, ts_final);

	elapsed = (elapsed1 + elapsed2);
	percall = (elapsed / uuids_len);
	printf(" done in %.9LF seconds (~%.9LF per).\n", elapsed, percall);

	printf("Printing the first 8 UUIDs:\n");
	for (size_t i = 0; i < 8; ++i) {
		char buf1[80];
		uuid7_to_string(buf1, 80, uuid7s[i]);
		printf("%zu: %s\n", i, buf1);
	}
	printf("\nDecoding the first 8 UUIDs:\n");
	for (size_t i = 0; i < 8; ++i) {
		char buf2[80];
		uuid7_decode(buf2, 80, uuid7s[i]);
		printf("%zu: %s\n", i, buf2);
	}

#ifndef UUID7_SKIP_MUTEX
	uuid7_mutex_destroy();
#endif
	free(ts);

	return 0;
}
