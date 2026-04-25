#ifndef LOG_H
#define LOG_H

#include <stdarg.h>

enum log_level_t {
	LOG_DEBUG,
	LOG_INFO,
	LOG_WARN,
	LOG_ERROR
};

void os_log(enum log_level_t level, const char *component, const char *fmt, ...);
void os_vlog(enum log_level_t level, const char *component, const char *fmt, va_list args);

#endif
