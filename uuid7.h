/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2024 Eric Herman <eric@freesa.org> */

#ifndef UUID7_H
#define UUID7_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

uint8_t *uuid7(uint8_t *ubuf);

char *uuid7_to_string(char *buf, size_t buf_size, const uint8_t *bytes);

#ifdef UUID7_WITH_MUTEX
int uuid7_mutex_init(void);
void uuid7_mutex_destroy(void);
#endif

struct uuid7 {
	uint64_t seconds:36;
	uint16_t hifrac:12;
	uint8_t uuid_ver:4;
	uint16_t lofrac:12;
	uint8_t uuid_var:2;
	uint16_t hiseq:6;
	uint16_t loseq:8;
	uint16_t segment:16;
	uint32_t rand:32;
};
struct uuid7 *uuid7_parts(struct uuid7 *u, const uint8_t *bytes);

extern const uint8_t uuid7_version;
extern const uint8_t uuid7_variant;

#ifdef __cplusplus
}
#endif
#endif
