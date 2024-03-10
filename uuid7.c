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
static_assert(sizeof(union uuid7) == 16);
static_assert(sizeof(union uuid7) == sizeof(uuid7_last));

const uint8_t uuid7_version = 7;
const uint8_t uuid7_variant = 1;

uint8_t *uuid7_next(uint8_t *ubuf, struct timespec ts, uint64_t random_bytes,
		    uint8_t *last_issued)
{
	assert(ubuf);

	/*
	   With only 24 bits of the fraction second,
	   wanted are the 24 most significant bits,
	   which might not be zero.
	   Valid values are 0 to 999999 or:
	   const uint32_t nine9s = 0x3B9AC9FF;
	   Because the bits 31 and 30 are never set,
	   only bits 0-29, the bits to extract are 6 through 29:
	   0011 1011 1001 1010 1100 1001 1111 1111
	   --++ ++++ ++++ ++++ ++++ ++++ ++-- ----
	   3    F    F    F    F    F    C    0
	 */

	union uuid7 tmp;
	tmp.seconds = (((uint64_t)ts.tv_sec) & 0x0000000FFFFFFFFF);
	tmp.hifrac = ((((uint32_t)ts.tv_nsec) & 0x3FFC0000) >> (4 * 4));
	tmp.uuid_ver = (uuid7_version & 0x0F);
	tmp.lofrac = ((((uint32_t)ts.tv_nsec) & 0x0003FFC0) >> (4 + 2));
	tmp.uuid_var = (uuid7_variant & 0x03);
	tmp.sequence = 0;
	tmp.rand = (random_bytes & 0x0000FFFFFFFFFFFF);

	/* the first 8 bytes contain the seconds and the fraction */
	static_assert((8 * 8) == (36 + 12 + 4 + 12));
	if (memcmp(last_issued, &tmp.bytes, 8) == 0) {
		uint16_t seq = 0;
		seq = (((uint16_t)(last_issued[8] & 0x3F)) << 8)
		    | (last_issued[9]);
		if (seq >= 0x3FFF) {
			/*
			   A 10 Ghz CPU is 10 cycles per nanosecond.
			   Even with multiple instructions per cycle,
			   more than 16383 in the same 50 nanoseconds
			   suggests something is wrong with th3 clockid
			 */
			return NULL;
		}
		++seq;
		tmp.sequence = seq;
	}

	if (!memcpy(last_issued, tmp.bytes, 16)) {
		return NULL;
	}

	if (!memcpy(ubuf, tmp.bytes, 16)) {
		return NULL;
	}

	return ubuf;
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
char *uuid7_to_string(char *buf, size_t buflen, const uint8_t *bytes)
{
	const size_t uuid_str_size = ((16 * 2) + 4) + 1;
	if (buflen < uuid_str_size) {
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
