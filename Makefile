# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 Eric Herman <eric@freesa.org>

default: demo

SHELL=/bin/bash

# extracted from https://github.com/torvalds/linux/blob/master/scripts/Lindent
LINDENT=indent -npro -kr -i8 -ts8 -sob -l80 -ss -ncs -cp1 -il0

CFLAGS += -g -Wall -Wextra -Wpedantic -Werror -pipe

uuid7.c: uuid7.h

demo: uuid7.c demo.c
	cc -g -Wall -Wextra -Wpedantic -Werror -pipe \
		uuid7.c demo.c \
		-o demo
	./demo

.PHONY: tidy
tidy:
	$(LINDENT) \
		-T FILE \
		-T size_t -T ssize_t \
		-T uint8_t -T int8_t \
		-T uint16_t -T int16_t \
		-T uint32_t -T int32_t \
		-T uint64_t -T int64_t \
		*.h *.c

.PHONY: clean
clean:
	rm -rf *.o demo *~
