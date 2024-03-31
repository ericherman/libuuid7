/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2024 Eric Herman <eric@freesa.org> */

#include "uuid7.h"

#include <string.h>
#include <sys/types.h>

#ifndef uuid7_getrandom
#include <sys/random.h>
// for UUID7_DEBUG, allow swapping the getrandom function at runtime
#ifdef UUID7_DEBUG
ssize_t (*uuid7_getrandom)(void *buf, size_t buflen, unsigned int flags)
    = getrandom;
#else
#define uuid7_getrandom getrandom
#endif
#endif

#ifndef uuid7_clock_gettime
#include <time.h>
// for UUID7_DEBUG, allow swapping the clock_gettime function at runtime
#ifdef UUID7_DEBUG
int (*uuid7_clock_gettime)(clockid_t clockid, struct timespec *tp)
    = clock_gettime;
#else
#define uuid7_clock_gettime clock_gettime
#endif
#endif

#ifndef UUID7_CLOCKID
#define UUID7_CLOCKID CLOCK_REALTIME
#endif

// for UUID7_DEBUG, allow changing the uuid7_clockid at runtime, otherwise const
#ifndef UUID7_DEBUG
const
#endif
clockid_t uuid7_clockid = UUID7_CLOCKID;

#ifndef UUID7_NO_THREADS
#ifdef ARDUINO
#define UUID7_NO_THREADS 1
#else
#define UUID7_NO_THREADS 0
#include <threads.h>
#endif
#endif

#if (UUID7_NO_THREADS)
#ifdef UUID7_WITH_MUTEX
#error UUID7_WITH_MUTEX does not make sense with UUID7_NO_THREADS
#endif
#endif

#ifdef UUID7_WITH_MUTEX
#include <stdbool.h>
static bool uuid7_mutex_initd = false;
static mtx_t uuid7_mutex;
#endif

static
#ifndef UUID7_WITH_MUTEX
#if (!UUID7_NO_THREADS)
 thread_local
#endif
#endif
uint8_t uuid7_last[16] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/*
   If the clock has gone backwards in time by a large amount,
   the system may not catch up in a reasonable time, and thus
   it may be more desirable to generate UUIDs that would sort
   prior to UUIDs already generated.

   In that situation, uuid7_reset can be used.

   However, if not UUID7_WITH_MUTEX and not UUID7_NO_THREADS,
   then this will need to be called for each thread which has
   already set the thread_local uuid7_last.
*/
void uuid7_reset(void)
{
#ifdef UUID7_WITH_MUTEX
	if (uuid7_mutex_initd) {
		mtx_lock(&uuid7_mutex);
	}
#endif

	memset(uuid7_last, 0x00, 16);

#ifdef UUID7_WITH_MUTEX
	if (uuid7_mutex_initd) {
		mtx_unlock(&uuid7_mutex);
	}
#endif
}

#include <assert.h>
/*
#ifndef static_assert
#define static_assert _Static_assert
#endif
*/
static_assert(sizeof(struct uuid7) == 16);
static_assert(sizeof(uuid7_last) == 16);

const uint8_t uuid7_version = 7;
const uint8_t uuid7_variant = 1;

uint8_t *uuid7_next(uint8_t *ubuf, struct timespec ts, uint16_t segment,
		    uint32_t random_bytes, uint8_t *last_issued)
{
	assert(ubuf);
	assert(ts.tv_nsec >= 0 && ts.tv_nsec <= 999999999);
	int success = 0;

	/*
	   With only 24 bits of the fraction second,
	   wanted are the 24 most significant bits,
	   which might not be zero.
	   Valid values are 0 to 999999999 or:
	   const uint32_t nine9s = 0x3B9AC9FF;
	   Because the bits 31 and 30 are never set,
	   only bits 0-29, the bits to extract are 6 through 29:
	   0011 1011 1001 1010 1100 1001 1111 1111
	   --++ ++++ ++++ ++++ ++++ ++++ ++-- ----
	   3    F    F    F    F    F    C    0

	   However, since modern clocks really are nanosecond time,
	   we will put those last 6 bits from the time in to the 6
	   high bits of the 14 bit sequence number, 255 values for
	   sequences created in the same nanosecond.
	 */

	uint64_t seconds = (((uint64_t)ts.tv_sec) & 0x0000000FFFFFFFFF);
	uint16_t hifrac =
	    ((((uint32_t)ts.tv_nsec) & 0x3FFC0000) >> (2 + (4 * 4)));

	uint16_t lofrac = ((((uint32_t)ts.tv_nsec) & 0x0003FFC0) >> (4 + 2));
	uint8_t hiseq = (ts.tv_nsec & 0x3F);

	ubuf[0] = (seconds & 0x0000000FF0000000) >> (7 * 4);
	ubuf[1] = (seconds & 0x000000000FF00000) >> (5 * 4);
	ubuf[2] = (seconds & 0x00000000000FF000) >> (3 * 4);
	ubuf[3] = (seconds & 0x0000000000000FF0) >> (1 * 4);
	ubuf[4] = (((seconds & 0x000000000000000F) << (1 * 4))
		   | ((hifrac & 0x0F00) >> (2 * 4)));
	ubuf[5] = (hifrac & 0x00FF);
	ubuf[6] = (((uuid7_version & 0x0F) << 4)
		   | ((lofrac & 0x0F00) >> (2 * 4)));
	ubuf[7] = (lofrac & 0x00FF);
	ubuf[8] = ((uuid7_variant & 0x03) << 6) | hiseq;
	ubuf[9] = 0x00;
	ubuf[10] = ((segment & 0xFF00) >> 8);
	ubuf[11] = ((segment & 0x00FF));
	ubuf[12] = (random_bytes & 0x00000000000000FF) >> (0 * 8);
	ubuf[13] = (random_bytes & 0x000000000000FF00) >> (1 * 8);
	ubuf[14] = (random_bytes & 0x0000000000FF0000) >> (2 * 8);
	ubuf[15] = (random_bytes & 0x00000000FF000000) >> (3 * 8);

#ifdef UUID7_WITH_MUTEX
	if (uuid7_mutex_initd) {
		mtx_lock(&uuid7_mutex);
	}
#endif

	/* the first 9 bytes contain the seconds and the fraction */
	static_assert((9 * 8) == (36 + 12 + 4 + 12 + 2 + 6));
	int cmp = memcmp(last_issued, ubuf, 9);
	if (cmp > 0) {
		/*
		   Sadly, we've gone backwards in time.
		   The caller will have to try again when time catches up

		   If they are very chummy with the library,
		   they can declare, and call a non-API "friend" function:
		   void uuid7_reset(void);
		   and then call that before re-trying.
		 */
		goto uuid7_next_end;
	}
	if (cmp == 0) {
		uint16_t seq = 1 + last_issued[9];
		if (seq <= 0xFF) {
			ubuf[9] = seq;
		} else {
			ubuf[9] = 0xFF;
			/*
			   A 10 Ghz CPU is 10 cycles per nanosecond.
			   Even with multiple instructions per cycle,
			   more than 16383 in the same 64 nanoseconds
			   suggests something is wrong with the clockid

			   Perhaps the caller will be lucky and the
			   random bits will sort naturally, we shall see
			 */
			if (memcmp(last_issued, ubuf, 16) >= 0) {
				/* the caller was NOT lucky, this is a fail */
				goto uuid7_next_end;
			}
		}
	}

	void *dest = memcpy(last_issued, ubuf, 16);
	assert(dest);
	(void)dest;

	success = 1;

uuid7_next_end:

#ifdef UUID7_WITH_MUTEX
	if (uuid7_mutex_initd) {
		mtx_unlock(&uuid7_mutex);
	}
#endif

	if (!success) {
		memset(ubuf, 0x00, 16);
		return NULL;
	}

	return ubuf;
}

struct uuid7 *uuid7_parts(struct uuid7 *u, const uint8_t *bytes)
{
	assert(u);
	assert(bytes);

	u->seconds = ((((uint64_t)bytes[0]) << (7 * 4))
		      | (((uint64_t)bytes[1]) << (5 * 4))
		      | (((uint64_t)bytes[2]) << (3 * 4))
		      | (((uint64_t)bytes[3]) << (1 * 4))
		      | (((uint64_t)bytes[4]) >> (1 * 4)));

	u->hifrac = ((((uint16_t)bytes[4] & 0x0F) << 8) | bytes[5]);
	u->uuid_ver = (bytes[6] & 0xF0) >> 4;
	u->lofrac = (((uint16_t)(bytes[6] & 0x0F)) << 8) | bytes[7];
	u->uuid_var = (bytes[8] & 0xC0) >> 6;
	u->hiseq = bytes[8] & 0x3F;
	u->loseq = bytes[9];
	u->segment = (((uint16_t)bytes[10]) << 8) | bytes[11];

	u->rand = (((uint64_t)bytes[15]) << (8 * 3))
	    | (((uint64_t)bytes[14]) << (8 * 2))
	    | (((uint64_t)bytes[13]) << (8 * 1))
	    | (((uint64_t)bytes[12]) << (8 * 0));

	return u->uuid_ver == uuid7_version && u->uuid_var == uuid7_variant
	    ? u : NULL;
}

uint8_t *uuid7(uint8_t *ubuf)
{
	struct timespec ts;
	if (uuid7_clock_gettime(uuid7_clockid, &ts)) {
		memset(ubuf, 0x00, 16);
		return NULL;
	}

	uint64_t random_bytes = 0;
	size_t size = sizeof(random_bytes);
	unsigned int flags = 0;
	ssize_t rndbytes = uuid7_getrandom(&random_bytes, size, flags);
	if (rndbytes < 0 || ((size_t)rndbytes != size)) {
		memset(ubuf, 0x00, 16);
		return NULL;
	}
#ifdef UUID7_WITH_MUTEX
	/* everything is mutexed, there is no segmenting */
	uint16_t segment = (uint16_t)(random_bytes >> (4 * 8));
#elif UUID7_NO_THREADS
	/* there is only one thread, there is no segmenting */
	uint16_t segment = (uint16_t)(random_bytes >> (4 * 8));
#else
	/* segment by address of thread_local */
	uint16_t segment = 0;
	for (size_t i = 1; i < (sizeof(uintptr_t) / 2); ++i) {
		segment = segment ^ (((uintptr_t) uuid7_last) >> (16 * i));
	}
#endif

	uint32_t rand32 = (0xFFFFFFFF & random_bytes);
	return uuid7_next(ubuf, ts, segment, rand32, uuid7_last);
}

static char uuid7_nibble_to_hex(uint8_t nib)
{
	assert(nib < 16);
	char rv = 0;
	rv = (nib < 10) ? '0' + nib : 'a' + (nib - 10);
	assert((rv >= '0' && rv <= '9') || (rv >= 'a' && rv <= 'f'));
	return rv;
}

static size_t uuid7_minz(size_t a, size_t b)
{
	return (a < b) ? a : b;
}

/* 8-4-4-4-12 */
char *uuid7_to_string(char *buf, size_t buf_size, const uint8_t *bytes)
{
	assert(buf);
	const size_t uuid_str_size = ((16 * 2) + 4) + 1;
	memset(buf, 0x00, uuid7_minz(uuid_str_size, buf_size));
	if (buf_size < uuid_str_size) {
		return NULL;
	}
	size_t pos = 0;
	for (size_t i = 0; i < 16; ++i) {
		uint8_t byte = bytes[i];
		uint8_t hi_nib = ((byte & 0xF0) >> 4);
		uint8_t lo_nib = (byte & 0x0F);
		buf[pos++] = uuid7_nibble_to_hex(hi_nib);
		buf[pos++] = uuid7_nibble_to_hex(lo_nib);
		switch (i) {
		case 3:
		case 3 + 2:
		case 3 + 2 + 2:
		case 3 + 2 + 2 + 2:
			buf[pos++] = '-';
		}
	}
	return buf;
}

#ifdef UUID7_WITH_MUTEX
int uuid7_mutex_init(void)
{
	int rv = mtx_init(&uuid7_mutex, mtx_plain);
	if (rv == thrd_success) {
		uuid7_mutex_initd = true;
	}
	return rv;
}

void uuid7_mutex_destroy(void)
{
	mtx_destroy(&uuid7_mutex);
	uuid7_mutex_initd = false;
}
#endif
