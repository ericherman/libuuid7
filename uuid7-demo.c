/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2024 Eric Herman <eric@freesa.org> */

#include "uuid7.h"

#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <time.h>

char *uuid7_decode(char *buf, size_t buflen, const uint8_t *bytes)
{
	struct uuid7 tmp;

	uuid7_parts(&tmp, bytes);

	uint32_t nanos = (((uint32_t)tmp.hifrac) << 18)
	    | (((uint32_t)tmp.lofrac) << 6)
	    | tmp.hiseq;

	const char *fmt =
	    "%" PRIu64 ".%" PRIu32 " [%" PRIu16 "] (%" PRIu8 ",%" PRIu8
	    ") [%04" PRIx16 "] %08" PRIx32;

	snprintf(buf, buflen, fmt, tmp.seconds, nanos, tmp.loseq,
		 tmp.uuid_ver, tmp.uuid_var, tmp.segment, tmp.rand);

	if ((tmp.uuid_ver != uuid7_version)
	    || (tmp.uuid_var != uuid7_variant)) {
		return NULL;
	}
	return buf;
}

void err(const char *file, long line, const char *func, int err, char *fmt, ...)
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

#define Err(...) \
	err(__FILE__, __LINE__, __func__, errno, __VA_ARGS__)

#define Die(...) do { \
	err(__FILE__, __LINE__, __func__, errno, __VA_ARGS__); \
	exit(EXIT_FAILURE); \
} while (0)

// we don't really need this boiler-plate to be this verbose
static void err_on_thread_create_failure(const char *file, long line,
					 const char *func, int errn,
					 int create_return_code, size_t our_id)
{
	switch (create_return_code) {
	case thrd_success:
		break;
	case thrd_nomem:
		err(file, line, func, errn,	//
		    "thrd_create %zu: thrd_nomem", our_id);
		break;
	case thrd_error:
		err(file, line, func, errn,	//
		    "thrd_create %zu: thrd_error", our_id);
		break;
	case thrd_timedout:
		err(file, line, func, errn,	//
		    "thrd_create %zu: unexpected thrd_timedout", our_id);
		break;
	case thrd_busy:
		err(file, line, func, errn,	//
		    "thrd_create %zu: unexpected thrd_busy", our_id);
		break;
	default:
		err(file, line, func, errn,	//
		    "thrd_create %zu: unexpected value %d", our_id,
		    create_return_code);
		break;
	}
}

#define Die_on_thread_create_failure(error, our_id) do { \
	err_on_thread_create_failure(__FILE__, __LINE__, __func__, errno, \
		error, our_id); \
	exit(EXIT_FAILURE); \
} while (0)

static clockid_t fd_to_clockid(unsigned int fd)
{
	const unsigned long clockfd = 3;
	return ((~(clockid_t) (fd) << 3) | clockfd);
}

static int clockid_to_fd(clockid_t clk)
{
	return ((unsigned int)~((clk) >> 3));
}

extern const clockid_t uuid7_clockid;

#define elapsed_ts(begin, until) \
	( (until.tv_sec + (until.tv_nsec / (long double)1000000000.0)) \
	- (begin.tv_sec + (begin.tv_nsec / (long double)1000000000.0)) )

struct timespec_task {
	clockid_t clockid;
	struct timespec *ts;
	size_t ts_len;
};

int ts_task_thread_func(void *context)
{
	struct timespec_task *task = (struct timespec_task *)context;
	for (size_t i = 0; i < task->ts_len; ++i) {
		if (clock_gettime(task->clockid, &task->ts[i])) {
			Die("clock_gettime(%ld, &ts)", (long)task->clockid);
		}
	}
	return EXIT_SUCCESS;
}

int timespec_compare(const void *a, const void *b)
{
	const struct timespec *ts_a = (const struct timespec *)a;
	const struct timespec *ts_b = (const struct timespec *)b;
	time_t sec_diff = ts_a->tv_sec - ts_b->tv_sec;
	return sec_diff ? sec_diff : ts_a->tv_nsec - ts_b->tv_nsec;
}

struct uuid7_task {
	uint8_t *buf;
	size_t bufz;
	size_t retries;
};

int uuid7_task_thread_func(void *context)
{
	struct uuid7_task *task = (struct uuid7_task *)context;
	size_t uuid_bytes = 16;
	size_t num = task->bufz / uuid_bytes;
	int errors = 0;
	for (size_t i = 0; i < num; ++i) {
		uint8_t *uuid7_buf = task->buf + (i * uuid_bytes);
		uint8_t *rv = NULL;
		size_t max_tries = 100;
		for (size_t j = 0; !rv && j < max_tries; ++j) {
			task->retries = j;
			if (j > 50) {
				struct timespec snooze = { 0, 2 };
				nanosleep(&snooze, NULL);
			}
			rv = uuid7(uuid7_buf);
		}
		if (!rv) {
			Err("uuid7(%p) failed", uuid7_buf);
			++errors;
		}
	}
	return errors ? EXIT_FAILURE : EXIT_SUCCESS;
}

int memcmp16(const void *a, const void *b)
{
	return memcmp(a, b, 16);
}

static long double per_second(long double quantity, long double elapsed_seconds)
{
	return quantity / elapsed_seconds;
}

#include <fcntl.h>
int main(int argc, char **argv)
{
	clockid_t clockid;
	size_t num_threads = 0;
	if (argc > 1) {
		int i = atoi(argv[1]);
		if (i >= 0) {
			num_threads = (unsigned)i;
		}
	}
	num_threads = num_threads ? num_threads : 16;

	size_t subset = 8;
	if (argc > 2) {
		int i = atoi(argv[2]);
		if (i >= 0) {
			subset = (unsigned)i;
		}
	}
	subset = subset ? subset : 8;

	if (argc > 3) {
		// e.g.: "/dev/ptp0"
		const char *dev_clock = argv[3];
		int fd = open(dev_clock, O_RDWR);
		if (fd < 0) {
			Die("open(\"%s\", O_RDWR)", dev_clock);
		}
		clockid = fd_to_clockid(fd);
	} else {
		clockid = uuid7_clockid;
	}
	int clockfd = clockid_to_fd(clockid);
	if (clockfd >= 0) {
		printf("clockid fd: %d\n", clockid_to_fd(clockid));
	}

	thrd_t *thread_ids = (thrd_t *) calloc(num_threads, sizeof(thrd_t));
	if (!thread_ids) {
		size_t ti_size = num_threads * sizeof(thrd_t);
		Die("failed to allocate %zu bytes?", ti_size);
	}
	size_t per_thread_len = 100 * 1000;
	const size_t ts_len = num_threads * per_thread_len;
	struct timespec *ts =
	    (struct timespec *)calloc(ts_len, sizeof(struct timespec));
	if (!ts) {
		size_t ts_size = ts_len * sizeof(struct timespec);
		Die("failed to allocate %zu bytes?", ts_size);
	}
	printf("Checking the clock ...");
	if (clock_getres(clockid, &ts[0])) {
		Die("clock_getres(%ld, &ts)", (long)clockid);
	}
	printf(" done.\n");

	printf("    resolution:  %jd.%09ld\n", (intmax_t) ts[0].tv_sec,
	       ts[0].tv_nsec);

	struct timespec_task *ts_contexts =
	    (struct timespec_task *)calloc(num_threads,
					   sizeof(struct timespec_task));
	if (!ts_contexts) {
		size_t ctx_size = sizeof(struct timespec_task) * num_threads;
		Die("failed to allocate %zu bytes?", ctx_size);
	}
	for (size_t i = 0; i < num_threads; ++i) {
		size_t offset = i * per_thread_len;
		ts_contexts[i].clockid = clockid;
		ts_contexts[i].ts = ts + offset;
		ts_contexts[i].ts_len = per_thread_len;
	}

	printf("Calling clock_gettime in a tight loop"
	       " %zu times in %zu threads\n" "\tfor a total of %zu calls ...",
	       per_thread_len, num_threads, ts_len);
	fflush(stdout);

	struct timespec ts_begin;
	struct timespec ts_final;

	clock_gettime(clockid, &ts_begin);

	for (size_t i = 0; i < num_threads; ++i) {
		void *context = &(ts_contexts[i]);
		thrd_start_t thread_func = ts_task_thread_func;
		thrd_t *thread_id = &(thread_ids[i]);

		int error = thrd_create(thread_id, thread_func, context);
		if (error) {
			Die_on_thread_create_failure(error, i);
		}
	}
	for (size_t i = 0; i < num_threads; ++i) {
		thrd_join(thread_ids[i], NULL);
	}

	clock_gettime(clockid, &ts_final);
	long double elapsed = elapsed_ts(ts_begin, ts_final);
	long double percall = (elapsed / ts_len);
	printf("\n\t\tdone in %.9LF seconds\n"
	       "\t\t\t(~%.9LF each, %.0LF per second).\n", elapsed, percall,
	       per_second(ts_len, elapsed));

	free(ts_contexts);
	ts_contexts = NULL;

	qsort(ts, ts_len, sizeof(struct timespec), timespec_compare);

	size_t duplicates = 0;
	for (size_t i = 1; i < ts_len; ++i) {
		if ((ts[i - 1].tv_sec == ts[i].tv_sec)
		    && (ts[i - 1].tv_nsec == ts[i].tv_nsec)) {
			++duplicates;
		}
	}
	printf("\tfor %zu calls to clock_gettime,\n", ts_len);
	printf("\t\t%zu duplicates were found ( %3.1f%% )\n", duplicates,
	       100 * (duplicates * 1.0) / ts_len);
	if (duplicates) {
		printf("\t( sequence may not always be zero,\n");
		printf("\t\tor may need to be distinguished by segment )\n");
	} else {
		printf("\t(sequence will probably always be zero)\n");
	}

	printf("First %zu of combined and sorted results:\n", subset);
	for (size_t i = 0; i < subset; ++i) {
		printf("\t%10jd.%09ld\n", (intmax_t) ts[i].tv_sec,
		       ts[i].tv_nsec);
	}

	size_t uuids_per_thread_len = (10 * 1000);
	size_t uuid7_bytes = 16;
	size_t uuids_per_thread_size = uuids_per_thread_len * uuid7_bytes;
	size_t uuids_len = uuids_per_thread_len * num_threads;
	size_t uuids_size = uuids_per_thread_size * num_threads;
	uint8_t *uuid7s = (uint8_t *)calloc(1, uuids_size);
	if (!uuid7s) {
		Die("failed to allocate %zu bytes", uuids_size);
	}
	size_t ut_size = sizeof(struct uuid7_task) * num_threads;
	struct uuid7_task *uuid7_tasks =
	    (struct uuid7_task *)calloc(1, ut_size);
	if (!uuid7_tasks) {
		Die("failed to allocate %zu bytes", ut_size);
	}
	for (size_t i = 0; i < num_threads; ++i) {
		size_t offset = i * uuids_per_thread_size;
		uuid7_tasks[i].buf = uuid7s + offset;
		uuid7_tasks[i].bufz = uuids_per_thread_size;
	}

	printf("\n\nGenerating %zu UUIDs across %zu threads...", uuids_len,
	       num_threads);
	fflush(stdout);
#ifdef UUID7_WITH_MUTEX
	uuid7_mutex_init();
#endif

	clock_gettime(clockid, &ts_begin);

	for (size_t i = 0; i < num_threads; ++i) {
		void *context = &(uuid7_tasks[i]);
		thrd_start_t thread_func = uuid7_task_thread_func;
		thrd_t *thread_id = &(thread_ids[i]);

		int error = thrd_create(thread_id, thread_func, context);
		if (error) {
			Die_on_thread_create_failure(error, i);
		}
	}
	for (size_t i = 0; i < num_threads; ++i) {
		thrd_join(thread_ids[i], NULL);
	}

	clock_gettime(clockid, &ts_final);
	elapsed = elapsed_ts(ts_begin, ts_final);
	percall = (elapsed / uuids_len);
	printf("\n\tdone in %.9LF seconds (~%.9LF each, %.0LF per second).\n",
	       elapsed, percall, per_second(uuids_len, elapsed));

	size_t max_retries = 0;
	for (size_t i = 0; i < num_threads; ++i) {
		if (uuid7_tasks[i].retries > max_retries) {
			max_retries = uuid7_tasks[i].retries;
		}
	}
	if (max_retries > 0) {
		printf("\t(max_retries: %zu)\n", max_retries);
	}
	free(uuid7_tasks);

	qsort(uuid7s, uuids_len, uuid7_bytes, memcmp16);

	/* absolute duplicate */
	size_t same16 = 0;
	/* only last 4 random bytes differ */
	size_t same12 = 0;
	/* same nanos and sequence; differ by "segment" and "random" */
	size_t same10 = 0;
	/* same nanos; differ by sequence, segment, random */
	size_t same9 = 0;
	size_t display_start = 0;
	size_t display_start_reason = 0;
	for (size_t i = uuids_len - 1; i > 2; --i) {
		size_t offset_a = (i - 1) * uuid7_bytes;
		size_t offset_b = i * uuid7_bytes;
		uint8_t *a = uuid7s + offset_a;
		uint8_t *b = uuid7s + offset_b;

		if (memcmp(a, b, 16) == 0) {
			++same16;
			display_start = i - 1;
			display_start_reason = 16;
		} else if (memcmp(a, b, 12) == 0) {
			++same12;
			if (display_start_reason <= 12) {
				display_start = i - 1;
				display_start_reason = 12;
			}
		} else if (memcmp(a, b, 10) == 0) {
			++same10;
			if (display_start_reason <= 10) {
				display_start = i - 1;
				display_start_reason = 10;
			}
		} else if (memcmp(a, b, 9) == 0) {
			++same9;
			if (display_start_reason <= 9) {
				display_start = i - 1;
				display_start_reason = 9;
			}
		}
	}
	printf(" UUIDs with overlaps with at least one other entry...\n");
	printf("%9zu (%04.1f%%) true duplicates\n", same16,
	       100 * (same16 * 1.0) / uuids_len);
	printf("%9zu (%04.1f%%)"
	       " same nanos, sequence, segment, differ only by 4 random bytes\n",
	       same12, 100 * (same12 * 1.0) / uuids_len);
	printf("%9zu (%04.1f%%)"
	       " same nanos, sequence, differ only by segment, random bytes\n",
	       same10, 100 * (same10 * 1.0) / uuids_len);
	printf("%9zu (%04.1f%%)"
	       " same nanos, differ by sequence, segment, random bytes\n",
	       same9, 100 * (same9 * 1.0) / uuids_len);

	if (display_start + subset >= uuids_len) {
		display_start = uuids_len - (subset + 1);
	}
	size_t display_max = display_start + subset;

	printf("Printing %zu UUIDs starting from %zu:\n", subset,
	       display_start);
	for (size_t i = display_start; i < display_max; ++i) {
		char buf1[80];
		size_t offset = i * uuid7_bytes;
		uuid7_to_string(buf1, 80, uuid7s + offset);
		printf("%04zu: %s\n", i, buf1);
	}
	printf("\nDecoding %zu UUIDs starting from %zu:\n", subset,
	       display_start);
	for (size_t i = display_start; i < display_max; ++i) {
		char buf2[80];
		size_t offset = i * uuid7_bytes;
		uuid7_decode(buf2, 80, uuid7s + offset);
		printf("%04zu: %s\n", i, buf2);
	}

#ifdef UUID7_WITH_MUTEX
	uuid7_mutex_destroy();
#endif

	free(uuid7s);
	free(ts);
	free(thread_ids);

	return 0;
}
