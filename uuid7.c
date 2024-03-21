/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2024 Eric Herman <eric@freesa.org> */

#include "uuid7.h"

#include <string.h>
#include <sys/types.h>

#ifndef uuid7_getrandom
#include <sys/random.h>
#define uuid7_getrandom getrandom
#endif

#ifndef uuid7_clock_gettime
#include <time.h>
#define uuid7_clock_gettime clock_gettime
#endif

#ifndef UUID7_CLOCKID
#define UUID7_CLOCKID CLOCK_REALTIME
#endif
const clockid_t uuid7_clockid = UUID7_CLOCKID;

#ifndef UUID7_SKIP_MUTEX
#include <stdbool.h>
#include <threads.h>
static bool uuid7_mutex_initd = false;
static mtx_t uuid7_mutex;
#endif

static uint8_t uuid7_last[16] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

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

uint8_t *uuid7_next(uint8_t *ubuf, struct timespec ts, uint64_t random_bytes,
		    uint8_t *last_issued)
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
	 */

	uint64_t seconds = (((uint64_t)ts.tv_sec) & 0x0000000FFFFFFFFF);
	uint16_t hifrac =
	    ((((uint32_t)ts.tv_nsec) & 0x3FFC0000) >> (2 + (4 * 4)));

	uint16_t lofrac = ((((uint32_t)ts.tv_nsec) & 0x0003FFC0) >> (4 + 2));

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
	ubuf[8] = ((uuid7_variant & 0x03) << 6);
	ubuf[9] = 0x00;

	ubuf[10] = (random_bytes & 0x00000000000000FF) >> (0 * 8);
	ubuf[11] = (random_bytes & 0x000000000000FF00) >> (1 * 8);
	ubuf[12] = (random_bytes & 0x0000000000FF0000) >> (2 * 8);
	ubuf[13] = (random_bytes & 0x00000000FF000000) >> (3 * 8);
	ubuf[14] = (random_bytes & 0x000000FF00000000) >> (4 * 8);
	ubuf[15] = (random_bytes & 0x0000FF0000000000) >> (5 * 8);

#ifndef UUID7_SKIP_MUTEX
	if (uuid7_mutex_initd) {
		mtx_lock(&uuid7_mutex);
	}
#endif

	/* the first 8 bytes contain the seconds and the fraction */
	static_assert((8 * 8) == (36 + 12 + 4 + 12));
	if (memcmp(last_issued, ubuf, 8) == 0) {
		uint16_t seq = 0;
		seq = (((uint16_t)(last_issued[8] & 0x3F)) << 8)
		    | (last_issued[9]);
		if (seq >= 0x3FFF) {
			/*
			   A 10 Ghz CPU is 10 cycles per nanosecond.
			   Even with multiple instructions per cycle,
			   more than 16383 in the same 64 nanoseconds
			   suggests something is wrong with the clockid
			 */
			goto uuid7_next_end;
		}
		++seq;
		ubuf[8] = (((uuid7_variant & 0x03) << 6)
			   | ((seq & 0x3F00) >> 8));
		ubuf[9] = (seq & 0x00FF);
	}

	if (!memcpy(last_issued, ubuf, 16)) {
		goto uuid7_next_end;
	}

	success = 1;

uuid7_next_end:

#ifndef UUID7_SKIP_MUTEX
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
	u->sequence = (((uint16_t)(bytes[8] & 0x3F)) << 8) | bytes[9];

	u->rand = (((uint64_t)0x00) << (8 * 7))
	    | (((uint64_t)0x00) << (8 * 6))
	    | (((uint64_t)bytes[15]) << (8 * 5))
	    | (((uint64_t)bytes[14]) << (8 * 4))
	    | (((uint64_t)bytes[13]) << (8 * 3))
	    | (((uint64_t)bytes[12]) << (8 * 2))
	    | (((uint64_t)bytes[11]) << (8 * 1))
	    | (((uint64_t)bytes[10]) << (8 * 0));

	return u->uuid_ver == uuid7_version && u->uuid_var == uuid7_variant
	    ? u : NULL;
}

uint8_t *uuid7(uint8_t *ubuf)
{
	struct timespec ts;
	if (uuid7_clock_gettime(uuid7_clockid, &ts)) {
		return NULL;
	}

	uint64_t random_bytes = 0;
	size_t size = sizeof(random_bytes);
	unsigned int flags = 0;
	ssize_t rndbytes = uuid7_getrandom(&random_bytes, size, flags);
	if (rndbytes < 0 || ((size_t)rndbytes != size)) {
		return NULL;
	}

	return uuid7_next(ubuf, ts, random_bytes, uuid7_last);
}

static char uuid7_nibble_to_hex(uint8_t nib)
{
	assert(nib < 16);
	char rv = 0;
	rv = (nib < 10) ? '0' + nib : 'a' + (nib - 10);
	assert((rv >= '0' && rv <= '9') || (rv >= 'a' && rv <= 'f'));
	return rv;
}

/* 8-4-4-4-12 */
char *uuid7_to_string(char *buf, size_t buf_size, const uint8_t *bytes)
{
	const size_t uuid_str_size = ((16 * 2) + 4) + 1;
	if (buf_size < uuid_str_size) {
		return NULL;
	}
	size_t pos = 0;
	memset(buf, 0x00, uuid_str_size);
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

#ifndef UUID7_SKIP_MUTEX
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
