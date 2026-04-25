#include "log.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

static enum log_level_t configured_level(void)
{
	const char *level = getenv("OSSIM_LOG");

	if (level == NULL)
		return LOG_WARN;
	if (strcmp(level, "debug") == 0)
		return LOG_DEBUG;
	if (strcmp(level, "info") == 0)
		return LOG_INFO;
	if (strcmp(level, "warn") == 0)
		return LOG_WARN;
	if (strcmp(level, "error") == 0)
		return LOG_ERROR;

	return LOG_WARN;
}

static const char *level_name(enum log_level_t level)
{
	switch (level) {
	case LOG_DEBUG:
		return "DEBUG";
	case LOG_INFO:
		return "INFO";
	case LOG_WARN:
		return "WARN";
	case LOG_ERROR:
		return "ERROR";
	default:
		return "LOG";
	}
}

void os_vlog(enum log_level_t level, const char *component, const char *fmt, va_list args)
{
	if (level < configured_level())
		return;

	pthread_mutex_lock(&log_lock);
	fprintf(stderr, "[%s] %s: ", level_name(level), component);
	vfprintf(stderr, fmt, args);
	fprintf(stderr, "\n");
	pthread_mutex_unlock(&log_lock);
}

void os_log(enum log_level_t level, const char *component, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	os_vlog(level, component, fmt, args);
	va_end(args);
}
