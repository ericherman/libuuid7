/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2024 Eric Herman <eric@freesa.org> */

#ifndef UUID7_H
#define UUID7_H

#include <stdint.h>
#include <stddef.h>

uint8_t *uuid7(uint8_t *ubuf);
char *uuid7_to_string(char *buf, size_t buflen, const uint8_t *bytes);

#ifndef UUID7_SKIP_MUTEX
int uuid7_mutex_init(void);
void uuid7_mutex_destroy(void);
#endif

/* this union is for illustration, but it is not required for the API */
union uuid7 {
	uint8_t bytes[16];
	struct {
		uint64_t seconds:36;
		uint16_t hifrac:12;
		uint8_t uuid_ver:4;
		uint16_t lofrac:12;
		uint8_t uuid_var:2;
		uint16_t sequence:14;
		uint64_t rand:48;
	};
};

extern const uint8_t uuid7_version;
extern const uint8_t uuid7_variant;
#endif
