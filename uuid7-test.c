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

static uint32_t uuid7_nanos(struct uuid7 u)
{
	uint32_t nanos = (((uint32_t)u.hifrac) << 18)
	    | (((uint32_t)u.lofrac) << 6)
	    | u.hiseq;
	return nanos;
}

static char *uuid7_dump(char *buf, size_t buflen, const uint8_t *bytes)
{
	struct uuid7 tmp;
	uuid7_parts(&tmp, bytes);

	uint32_t nanos = uuid7_nanos(tmp);

	const char *fmt =	//
	    "{ seconds: %" PRIu64	//
	    ", hifrac: %" PRIu16	//
	    ", uuid_ver: %" PRIu8	//
	    ", lofrac: %" PRIu16	//
	    ", uuid_var: %" PRIu8	//
	    ", hiseq: %" PRIu8	//
	    ", loseq: %" PRIu8	//
	    ", rand: %" PRIu64	//
	    "} (nanos: %" PRIu32 ")";

	snprintf(buf, buflen, fmt, tmp.seconds, tmp.hifrac, tmp.uuid_ver,
		 tmp.lofrac, tmp.uuid_var, tmp.hiseq, tmp.loseq, tmp.rand,
		 nanos);

	return buf;
}

static void uuid7_errorf(const char *file, long line,
			 const char *func, int err, char *fmt, ...)
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
}

#define Fail(...) do { \
	uuid7_errorf(__FILE__, __LINE__, __func__, errno, __VA_ARGS__); \
	++failures; \
} while (0)

#define elapsed_timespec(begin, until) \
	( (until.tv_sec + (until.tv_nsec / (long double)1000000000.0)) \
	- (begin.tv_sec + (begin.tv_nsec / (long double)1000000000.0)) )

unsigned check_sortable(void)
{
	unsigned failures = 0;

	size_t uuids_len = 100;
	uint8_t uuid7s[100][16];
	memset(uuid7s, 0x00, uuids_len * 16);

	for (size_t i = 0; i < 5; ++i) {
		uuid7(uuid7s[i]);
	}

	struct timespec snooze = { 0, 100 };
	nanosleep(&snooze, NULL);

	for (size_t i = 5; i < uuids_len; ++i) {
		uuid7(uuid7s[i]);
	}

	struct timespec a = { 0, 0 };
	struct timespec b = { 0, 0 };
	char abuf[250];
	char bbuf[250];
	const size_t bufsize = sizeof(abuf);
	struct uuid7 atmp;
	struct uuid7 btmp;

	uuid7_parts(&atmp, uuid7s[0]);
	a.tv_sec = atmp.seconds;
	a.tv_nsec = (((uint32_t)atmp.hifrac) << 18)
	    | (((uint32_t)atmp.lofrac) << 6)
	    | atmp.hiseq;
	if (atmp.hifrac > 4095) {
		Fail("uuid[%zu] hifrac of %lu > 4095 (%s)", 0,
		     atmp.hifrac, uuid7_dump(abuf, bufsize, uuid7s[0]));
	}

	for (size_t i = 1; i < uuids_len; ++i) {
		uuid7_parts(&btmp, uuid7s[i]);
		if (btmp.hifrac > 4095) {
			Fail("uuid[%zu] hifrac of %lu > 4095 (%s)", i,
			     btmp.hifrac, uuid7_dump(bbuf, bufsize, uuid7s[i]));
		}
		b.tv_sec = btmp.seconds;
		b.tv_nsec = (((uint32_t)btmp.hifrac) << 18)
		    | (((uint32_t)btmp.lofrac) << 6)
		    | btmp.hiseq;
		long double elapsed = elapsed_timespec(a, b);

		if (!(elapsed >= 0.0)) {
			Fail("bad elapsed %lf between"
			     " uuid[%zu] and uuid[%zu]\n\t%s,\n\t%s",
			     elapsed, i - 1, i,
			     uuid7_dump(abuf, bufsize, uuid7s[i - 1]),
			     uuid7_dump(bbuf, bufsize, uuid7s[i]));
		}
		if (((elapsed == 0.0) && (atmp.loseq >= btmp.loseq))
		    || ((elapsed > 0.0) && (btmp.loseq != 0))) {
			Fail("bad sequence %u, %u between"
			     " uuid[%zu] and uuid[%zu]\n\t%s,\n\t%s",
			     atmp.loseq, btmp.loseq, i - 1, i,
			     uuid7_dump(abuf, bufsize, uuid7s[i - 1]),
			     uuid7_dump(bbuf, bufsize, uuid7s[i]));
		}
		int rv = memcmp(uuid7s[i - 1], uuid7s[i], 16);
		if (rv >= 0) {
			for (size_t j = 0; j < 16; ++j) {
				int aj = uuid7s[i - 1][j];
				int bj = uuid7s[i][j];
				int cj = aj - bj;
				printf("%zu: 0x%02x, 0x%02x, (%d)\n",
				       j, aj, bj, cj);
			}
			Fail("bad memcmp (%d) between"
			     " uuid[%zu] and uuid[%zu]\n\t%s,\n\t%s",
			     rv, i - 1, i,
			     uuid7_dump(abuf, bufsize, uuid7s[i - 1]),
			     uuid7_dump(bbuf, bufsize, uuid7s[i]));
		}
		a = b;
		atmp = btmp;
	}
	return failures;
}

static unsigned uuid7_check_u64(const char *file, long line, const char *func,
				const char *name, uint64_t val, uint64_t expect)
{
	if (val == expect) {
		return 0;
	}

	const char *fmt = "FAIL: %s expected %" PRIx64 " but was %" PRIx64 "\n";
	fprintf(stderr, "%s:%ld %s(): ", file, line, func);
	fprintf(stderr, fmt, name, expect, val);

	return 1;
}

#define Check(actual, expected) \
	uuid7_check_u64(__FILE__, __LINE__, __func__, \
			#actual, ((uint64_t)actual), ((uint64_t)expected))

static unsigned uuid7_check_s(const char *file, long line, const char *func,
			      const char *name, const char *val,
			      const char *expect)
{
	if (strcmp(val, expect) == 0) {
		return 0;
	}

	const char *fmt =	//
	    "FAIL: %s\n"	//
	    "\texpected '%s'\n"	//
	    "\t but was '%s'\n";
	fprintf(stderr, "%s:%ld %s(): ", file, line, func);
	fprintf(stderr, fmt, name, expect, val);

	return 1;
}

#define Check_s(actual, expected) \
	uuid7_check_s(__FILE__, __LINE__, __func__, \
			#actual, actual, expected)

/* A "friend" function defined in uuid7.c, but not exposed in the API */
uint8_t *uuid7_next(uint8_t *ubuf, struct timespec ts, uint16_t segment,
		    uint32_t random_bytes, uint8_t *last_issued);

unsigned check_parts(void)
{
	unsigned failures = 0;

	uint8_t last[16];
	uint8_t ubuf[16];
	struct timespec ts;
	uint32_t random_bytes = ((((uint64_t)0x04) << (3 * 8))
				 | (((uint64_t)0x03) << (2 * 8))
				 | (((uint64_t)0x02) << (1 * 8))
				 | (((uint64_t)0x01) << (0 * 8))
	    );

	ts.tv_sec = 1711030306;
	ts.tv_nsec = 999999999;

	memset(last, 0x00, 16);
	memset(ubuf, '?', 16);

	uint16_t segment = 0x0102;
	uint8_t *rv = uuid7_next(ubuf, ts, segment, random_bytes, last);
	failures += Check((intptr_t)rv, (intptr_t)ubuf);

	struct uuid7 u;
	uuid7_parts(&u, ubuf);

	uint32_t nanos = uuid7_nanos(u);

	failures += Check(u.seconds, ts.tv_sec);
	failures += Check(nanos, ts.tv_nsec);
	failures += Check(u.uuid_ver, uuid7_version);
	failures += Check(u.uuid_var, uuid7_variant);
	failures += Check(u.loseq, 0);
	failures += Check(u.segment, segment);
	failures += Check(u.rand, random_bytes);

	failures += Check(memcmp(ubuf, last, 16), 0);

	uuid7_next(ubuf, ts, segment, random_bytes, last);
	uuid7_parts(&u, ubuf);

	nanos = uuid7_nanos(u);

	failures += Check(u.seconds, ts.tv_sec);
	failures += Check(nanos, ts.tv_nsec);
	failures += Check(u.uuid_ver, uuid7_version);
	failures += Check(u.uuid_var, uuid7_variant);
	failures += Check(u.loseq, 1);
	failures += Check(u.rand, random_bytes);

	size_t max_seq = 0xFF;
	for (size_t i = 2; i <= max_seq; ++i) {
		uuid7_next(ubuf, ts, segment, random_bytes, last);
		uuid7_parts(&u, ubuf);
		failures += Check(u.loseq, i);
	}

	rv = uuid7_next(ubuf, ts, segment, random_bytes, last);
	failures += Check((intptr_t)rv, (intptr_t)NULL);
	failures += Check_s((const char *)ubuf, "");

	return failures;
}

unsigned check_to_string(void)
{
	unsigned failures = 0;
	uint8_t bytes[16] = {
		0x01, 0x23, 0x45, 0x67,
		0x89, 0xab, 0x7c, 0xde,
		0x9f, 0x01, 0x23, 0x45,
		0x67, 0x89, 0xab, 0xcd
	};

	char buf[80];
	size_t bufz = sizeof(buf);

	size_t too_small = 7;
	failures +=
	    Check(((intptr_t)uuid7_to_string(buf, too_small, bytes)),
		  (intptr_t)NULL);
	failures += Check_s(buf, "");

	char *rv = uuid7_to_string(buf, bufz, bytes);
	failures += Check((intptr_t)rv, (intptr_t)buf);
	failures += Check_s(buf, "01234567-89ab-7cde-9f01-23456789abcd");

	return failures;
}

/* global variable defined in uuid7.c, but not exposed in uuid7.h */
extern clockid_t uuid7_clockid;

unsigned check_bad_clock_id(void)
{
	unsigned failures = 0;

	clockid_t orig_clockid = uuid7_clockid;
	uuid7_clockid = 1234567;	/* assumed to be bogus */

	uint8_t ubuf[16];
	memset(ubuf, '?', sizeof(ubuf));

	uint8_t *rv = uuid7(ubuf);
	failures += Check((intptr_t)rv, (intptr_t)NULL);

	uuid7_clockid = orig_clockid;

	return failures;
}

/* friend function defined in uui7.c, but not exposed in uuid7.h */
extern int (*uuid7_clock_gettime)(clockid_t clockid, struct timespec *tp);

time_t uuid7_test_bogus_clock_sec = 0;
long uuid7_test_bogus_clock_nsec = 0;
int uuid7_test_bogus_clock_rv = 0;
int uuid7_test_bogus_clock_gettime(clockid_t clockid, struct timespec *tp)
{
	(void)clockid;

	if (tp) {
		tp->tv_sec = uuid7_test_bogus_clock_sec;
		tp->tv_nsec = uuid7_test_bogus_clock_nsec;
	}

	return uuid7_test_bogus_clock_rv;
}

void uuid7_reset(void);

unsigned check_bad_gettime(void)
{
	unsigned failures = 0;

	int (*orig_gettime)(clockid_t clockid, struct timespec *tp) =
	    uuid7_clock_gettime;

	uuid7_clock_gettime = uuid7_test_bogus_clock_gettime;
	uuid7_test_bogus_clock_rv = 1;

	uint8_t ubuf[16];
	memset(ubuf, '?', sizeof(ubuf));

	uint8_t *rv = uuid7(ubuf);
	failures += Check((intptr_t)rv, (intptr_t)NULL);

	uuid7_clock_gettime = orig_gettime;

	uuid7_reset();

	return failures;
}

unsigned check_backwards_in_time(void)
{
	unsigned failures = 0;

	int (*orig_gettime)(clockid_t clockid, struct timespec *tp) =
	    uuid7_clock_gettime;

	uuid7_clock_gettime = uuid7_test_bogus_clock_gettime;
	uuid7_test_bogus_clock_sec = 102556800;
	uuid7_test_bogus_clock_nsec = 0;
	uuid7_test_bogus_clock_rv = 0;

	uint8_t ubuf[16];
	memset(ubuf, '?', sizeof(ubuf));

	uint8_t *rv = uuid7(ubuf);
	failures += Check((intptr_t)rv, (intptr_t)ubuf);

	/* set the clock backwards in time: */
	uuid7_test_bogus_clock_sec -= 1;

	rv = uuid7(ubuf);
	failures += Check((intptr_t)rv, (intptr_t)NULL);

	/* the clock catches back up: */
	uuid7_test_bogus_clock_sec += 1;

	rv = uuid7(ubuf);
	failures += Check((intptr_t)rv, (intptr_t)ubuf);

	/* set the clock very far backwards in time: */
	uuid7_test_bogus_clock_sec = 7776000;

	rv = uuid7(ubuf);
	failures += Check((intptr_t)rv, (intptr_t)NULL);

	/* very "chummy" with the library, call reset(): */
	uuid7_reset();

	rv = uuid7(ubuf);
	failures += Check((intptr_t)rv, (intptr_t)ubuf);

	uuid7_clock_gettime = orig_gettime;

	uuid7_reset();

	return failures;
}

/* friend function defined in uui7.c, but not exposed in uuid7.h */
extern ssize_t (*uuid7_getrandom)(void *buf, size_t buflen, unsigned int flags);

uint8_t *uuid7_test_getrandom_bytes = NULL;
size_t uuid7_test_getrandom_bytes_size = 0;
ssize_t uuid7_test_getrandom_rv = 0;
ssize_t uuid7_test_getrandom(void *buf, size_t bufz, unsigned int flags)
{
	(void)flags;

	if (uuid7_test_getrandom_rv < 0) {
		return uuid7_test_getrandom_rv;
	}

	if (uuid7_test_getrandom_bytes && uuid7_test_getrandom_bytes_size) {
		memcpy(buf, uuid7_test_getrandom_bytes,
		       bufz < uuid7_test_getrandom_bytes_size
		       ? bufz : uuid7_test_getrandom_bytes_size);
	}

	return uuid7_test_getrandom_rv;
}

unsigned check_bad_getrandom(void)
{
	unsigned failures = 0;

	ssize_t (*orig_getrandom)(void *buf, size_t buflen, unsigned int flags)
	    = uuid7_getrandom;

	uuid7_getrandom = uuid7_test_getrandom;
	uuid7_test_getrandom_rv = -1;

	uint8_t ubuf[16];
	memset(ubuf, '?', sizeof(ubuf));

	uint8_t *rv = uuid7(ubuf);
	failures += Check((intptr_t)rv, (intptr_t)NULL);

	uuid7_getrandom = orig_getrandom;

	return failures;
}

int main(void)
{
	unsigned failures = 0;

#ifdef UUID7_WITH_MUTEX
	uuid7_mutex_init();
#endif

	failures += check_sortable();
	failures += check_parts();
	failures += check_to_string();
	failures += check_bad_clock_id();
	failures += check_bad_gettime();
	failures += check_bad_getrandom();
	failures += check_backwards_in_time();

#ifdef UUID7_WITH_MUTEX
	uuid7_mutex_destroy();
#endif

	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
