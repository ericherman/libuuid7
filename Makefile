# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 Eric Herman <eric@freesa.org>

default: \
	build/uuid7-demo-with-mutex-static \
	build/uuid7-demo-static \
	build/uuid7-demo-dynamic \
	run-demo

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
UNAME := $(shell uname)

# VERSION=2.1.0
VERSION := $(shell tail -n1 VERSION | xargs)
VER_MAJOR := $(shell echo "$(VERSION)" | cut -f1 -d'.')
VER_MINOR := $(shell echo "$(VERSION)" | cut -f2 -d'.')
VER_PATCH := $(shell echo "$(VERSION)" | cut -f3 -d'.')

version:
	@echo VERSION: $(VERSION)
	@echo VER_MAJOR: $(VER_MAJOR)
	@echo VER_MINOR: $(VER_MINOR)
	@echo VER_PATCH: $(VER_PATCH)

ifeq ($(UNAME), Darwin)
SHAREDFLAGS = -dynamiclib
SHAREDEXT = dylib
else
SHAREDFLAGS = -shared
SHAREDEXT = so
endif

ifeq ("$(PREFIX)", "")
PREFIX=/usr/local
endif

ifeq ("$(LIBDIR)", "")
LIBDIR=$(PREFIX)/lib
endif

ifeq ("$(INCDIR)", "")
INCDIR=$(PREFIX)/include
endif

ifneq ($(strip $(srcdir)),)
   VPATH::=$(srcdir)
endif

LIB_NAME=libuuid7
A_NAME=libuuid7.a

SO_NAME=$(LIB_NAME).$(SHAREDEXT)
ifneq ($(UNAME), Darwin)
    SHAREDFLAGS += -Wl,-soname,$(SO_NAME)
endif

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
LDADD_BUILD := -luuid7


uuid7.c: uuid7.h

build:
	mkdir -pv build

build/uuid7.o: uuid7.c | build
	$(CC) -c -fPIC -I. $(CFLAGS_BUILD) $^ -o $@

build/uuid7-demo-static: build/uuid7.o uuid7-demo.c
	$(CC) -fPIC -I. $(CFLAGS_BUILD) $^ -o $@

build/uuid7-demo-dynamic: uuid7-demo.c build/$(SO_NAME)
	$(CC) -fPIC -I. -L build/ $(CFLAGS_BUILD) $< -o $@ $(LDADD_BUILD)

build/uuid7-demo-with-mutex-static: uuid7.c uuid7-demo.c
	$(CC) -DUUID7_WITH_MUTEX=1 $(CFLAGS_DEMO) $^ -o $@

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

build/$(SO_NAME): build/uuid7.o
	$(CC) $(SHAREDFLAGS) -o $(@).$(VER_MAJOR).$(VER_MINOR).$(VER_PATCH) $^
	cd build && \
		ln -sfv ./$(SO_NAME).$(VER_MAJOR).$(VER_MINOR).$(VER_PATCH) \
			./$(SO_NAME).$(VER_MAJOR).$(VER_MINOR)
	cd build && \
		ln -sfv ./$(SO_NAME).$(VER_MAJOR).$(VER_MINOR).$(VER_PATCH) \
			./$(SO_NAME).$(VER_MAJOR)
	cd build && \
		ln -sfv ./$(SO_NAME).$(VER_MAJOR).$(VER_MINOR).$(VER_PATCH) \
			./$(SO_NAME)
	ls -l $@
	readlink -f $@

build/$(A_NAME): build/uuid7.o
	ar -r build/$(A_NAME) $^

.PHONY: $(LIB_NAME)
$(LIB_NAME): build/$(SO_NAME) build/$(A_NAME)

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
run-demo: build/uuid7-demo-dynamic
	LD_LIBRARY_PATH=build/ $<

.PHONY: run-with-mutex
run-with-mutex: build/uuid7-demo-with-mutex-static
	$<

# extracted from https://github.com/torvalds/linux/blob/master/scripts/Lindent
LINDENT=indent -npro -kr -i8 -ts8 -sob -l80 -ss -ncs -cp1 -il0

.PHONY: tidy
tidy:
	patch -Np1 -i misc/workaround-indent-bug-65165.patch
	$(LINDENT) \
		-T timespec \
		-T timespec_task \
		-T uuid7_task \
		-T intptr_t \
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
