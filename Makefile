# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 Eric Herman <eric@freesa.org>

default: run-demo

# $@ : target label
# $< : the first prerequisite after the colon
# $^ : all of the prerequisite files
# $* : wildcard matched part

# |  : order-only prerequisites
#  https://www.gnu.org/software/make/manual/html_node/Prerequisite-Types.html

# dir, notdir :
#  https://www.gnu.org/software/make/manual/html_node/File-Name-Functions.html

CC ?= gcc
BROWSER	?= firefox

# pushd, popd are bash-ism
SHELL := /bin/bash

CFLAGS_NOISY := -Wall -Wextra -Wpedantic -Wcast-qual -Wc++-compat \
		$(CFLAGS) -pipe

CFLAGS_DEBUG := -g -O0 $(CFLAGS_NOISY) -Werror \
	-DUUID7_DEBUG \
	-fno-inline-small-functions \
	-fkeep-inline-functions \
	-fkeep-static-functions

CFLAGS_COVERAGE := $(CFLAGS_DEBUG) \
	-fprofile-arcs \
	-ftest-coverage

LDFLAGS_COVERAGE := --coverage
LDADD_COVERAGE := -lgcov

CFLAGS_BUILD := -g -O2 -DNDEBUG $(CFLAGS_NOISY)

uuid7.c: uuid7.h

build:
	mkdir -pv build

build/uuid7-demo: uuid7.c uuid7-demo.c | build
	$(CC) $(CFLAGS_DEMO) $^ -o $@

build/uuid7-demo-no-mutx: uuid7.c uuid7-demo.c | build
	$(CC) -DUUID7_SKIP_MUTEX=1 $(CFLAGS_DEMO) $^ -o $@

coverage:
	mkdir -pv coverage

coverage/uuid7.o: uuid7.c | coverage
	$(CC) -c -fPIC -I. $(CFLAGS_COVERAGE) -o $@ $<

coverage/uuid7-test: coverage/uuid7.o uuid7-test.c
	$(CC) -I. $(CFLAGS_COVERAGE) \
		-L ./coverage $(LDFLAGS_COVERAGE) \
		-o $@ $^ \
		$(LDADD_COVERAGE)

.PHONY: check-unit
check-unit: coverage/uuid7-test
	pushd coverage && ./uuid7-test
	@echo SUCCESS $@

coverage/uuid7.gcda: check-unit
	ls -l $@

coverage/uuid7.gcno: coverage/uuid7.gcda
	ls -l $@

coverage/coverage.info: uuid7.c \
		coverage/uuid7.gcda \
		coverage/uuid7.gcno
	lcov  --checksum \
		--capture \
		--base-directory . \
		--directory $(dir $@) \
		--output-file $@
	ls -l $@

coverage/tests/coverage_html/index.html: coverage/coverage.info
	mkdir -pv $(dir $@)
	genhtml $< --output-directory \
		./coverage/tests/coverage_html
	ls -l $@

coverage/tests/coverage_html/home/eric/src/libuuid7/uuid7.c.gcov.html: \
		coverage/tests/coverage_html/index.html
	ls -l $@

.PHONY: check-coverage
check-coverage: coverage/tests/coverage_html/home/eric/src/libuuid7/uuid7.c.gcov.html
	if [ $$(grep -c 'headerCovTableEntryHi">100.0 %' $< ) -eq 2 ]; then \
		true; \
	else grep headerCovTableEntryHi $< && \
		false; \
	fi
	@echo "SUCCESS $@"

.PHONY: view-coverage
view-coverage: coverage/tests/coverage_html/home/eric/src/libuuid7/uuid7.c.gcov.html
	$(BROWSER) $<


.PHONY: check
check: check-unit check-coverage
	@echo "SUCCESS $@"

.PHONY: run-demo
run-demo: build/uuid7-demo
	$<

.PHONY: run-no-mutex
run-no-mutex: build/uuid7-demo-no-mutx
	$<

# extracted from https://github.com/torvalds/linux/blob/master/scripts/Lindent
LINDENT=indent -npro -kr -i8 -ts8 -sob -l80 -ss -ncs -cp1 -il0

.PHONY: tidy
tidy:
	patch -Np1 -i misc/workaround-indent-bug-65165.patch
	$(LINDENT) \
		-T timespec \
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
	rm -rf *.o build coverage *~
