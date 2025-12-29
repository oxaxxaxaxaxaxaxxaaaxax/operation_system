#pragma once

#include <stdio.h>
#include <stdarg.h>

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO  = 1,
    LOG_LEVEL_WARN  = 2,
    LOG_LEVEL_ERROR = 3
} log_level_t;

#define LOG_DEBUG(fmt) logger_log(LOG_LEVEL_DEBUG, fmt)
#define LOG_INFO(fmt)  logger_log(LOG_LEVEL_INFO,  fmt)
#define LOG_WARN(fmt)  logger_log(LOG_LEVEL_WARN, fmt)
#define LOG_ERROR(fmt) logger_log(LOG_LEVEL_ERROR, fmt)

int logger_init(const char *path, log_level_t level);

void logger_shutdown(void);

void logger_set_level(log_level_t level);

void logger_log(log_level_t level, const char *fmt);

