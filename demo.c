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
	    ") %012x";

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

	printf("Checking the clock\n");
	struct timespec ts[10];
	struct timespec *ts0 = &ts[0];
	if (clock_getres(clockid, ts0)) {
		Die("clock_getres(%ld, &ts)", (long)clockid);
	}

	printf("    resolution:  %jd.%09ld\n", (intmax_t) ts0->tv_sec,
	       ts0->tv_nsec);

	printf("calling clock_gettime in a tight loop\n");
	for (size_t i = 0; i < 10; ++i) {
		if (clock_gettime(clockid, &ts[i])) {
			Die("clock_gettime(%ld, &ts)", (long)clockid);
		}
	}
	printf("results:\n");
	for (size_t i = 0; i < 10; ++i) {
		printf("\t%10jd.%09ld\n", (intmax_t) ts[i].tv_sec,
		       ts[i].tv_nsec);
	}

	printf("\n\ngetting UUIDs:\n");

	uint8_t uuid7s[10][16];
	memset(uuid7s, 0x00, 10 * 16);
	for (size_t i = 0; i < 5; ++i) {
		uuid7(uuid7s[i]);
	}
	struct timespec snooze = { 0, 100 };
	nanosleep(&snooze, NULL);
	for (size_t i = 5; i < 10; ++i) {
		uuid7(uuid7s[i]);
	}
	printf("printing UUIDs:\n");
	for (size_t i = 0; i < 10; ++i) {
		char buf1[80];
		uuid7_to_string(buf1, 80, uuid7s[i]);
		printf("%zu: %s\n", i, buf1);
	}
	printf("\ndecoding UUIDs:\n");
	for (size_t i = 0; i < 10; ++i) {
		char buf2[80];
		uuid7_decode(buf2, 80, uuid7s[i]);
		printf("%zu: %s\n", i, buf2);
	}

	return 0;
}
