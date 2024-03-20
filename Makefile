# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 Eric Herman <eric@freesa.org>

default: run-demo

SHELL=/bin/bash

# extracted from https://github.com/torvalds/linux/blob/master/scripts/Lindent
LINDENT=indent -npro -kr -i8 -ts8 -sob -l80 -ss -ncs -cp1 -il0

CFLAGS_NOISY := -Wall -Wextra -Wpedantic -Wcast-qual -Wc++-compat \
		$(CFLAGS) -pipe

CFLAGS_DEBUG := -g -O0 $(CFLAGS_NOISY) -Werror \
        -fno-inline-small-functions \
        -fkeep-inline-functions \
        -fkeep-static-functions

#        -fprofile-arcs \
#        -ftest-coverage \
#        --coverage \

# LDFLAGS_DEBUG := --coverage

CFLAGS_BUILD := -g -O2 -DNDEBUG $(CFLAGS_NOISY)
# LDFLAGS_BUILD :=

DEBUG ?= 0

ifeq ($(shell test $(DEBUG) -gt 0; echo $$?),0)
CFLAGS_DEMO := $(CFLAGS_DEBUG)
else
CFLAGS_DEMO := $(CFLAGS_BUILD)
endif

uuid7.c: uuid7.h

build/demo: uuid7.c demo.c
	mkdir -pv build
	cc $(CFLAGS_DEMO) $^ -o $@

build/demo-no-mutx: uuid7.c demo.c
	mkdir -pv build
	cc -DUUID7_SKIP_MUTEX=1 $(CFLAGS_DEMO) $^ -o $@

.PHONY: run_demo
run-demo: build/demo
	build/demo

.PHONY: run-no-mutex
run-no-mutex: build/demo-no-mutx
	build/demo-no-mutx

.PHONY: tidy
tidy:
	patch -Np1 -i misc/workaround-indent-bug-65165.patch
	$(LINDENT) \
		-T FILE \
		-T size_t -T ssize_t \
		-T uint8_t -T int8_t \
		-T uint16_t -T int16_t \
		-T uint32_t -T int32_t \
		-T uint64_t -T int64_t \
		*.h *.c
	patch -Rp1 -i misc/workaround-indent-bug-65165.patch

.PHONY: clean
clean:
	rm -rf *.o build *~
