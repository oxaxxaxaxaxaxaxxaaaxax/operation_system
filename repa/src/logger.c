
#include "logger.h"
#include <pthread.h>
#include <time.h>
#include <string.h>


static FILE *log_file = NULL;
static log_level_t current_level = LOG_LEVEL_INFO;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

static const char* level_to_str(log_level_t level)
{
    switch (level) {
        case LOG_LEVEL_DEBUG: return "DEBUG";
        case LOG_LEVEL_INFO:  return "INFO";
        case LOG_LEVEL_WARN:  return "WARN";
        case LOG_LEVEL_ERROR: return "ERROR";
        default:              return "UNK";
    }
}

int logger_init(const char *path, log_level_t level) {
    current_level = level;

    if (path == NULL) {
        log_file = stderr;
        return 0;
    }

    log_file = fopen(path, "a");
    if (log_file == NULL) {
        log_file = stderr;
        return -1;
    }
    return 0;
}

void logger_shutdown(void) {
    if (log_file != NULL && log_file != stderr) {
        fclose(log_file);
    }
    log_file = NULL;
}

void log_write(log_level_t level, const char *msg) {
    if (level < current_level) return;

    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);

    char ts[64];
    int res = strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tmv);
    if (res == 0){
        return;
    }

    fprintf(log_file, "%s [%s] %s\n", ts, level_to_str(level), msg);
    fflush(log_file);
}


void logger_log(log_level_t level, const char *msg){
    time_t now;
    struct tm tm_buf;
    char timestamp[32];

    if (!log_file) {
        log_file = stderr;
    }

    if (level < current_level) {
        return;
    }

    pthread_mutex_lock(&log_mutex);

    now = time(NULL);
    localtime_r(&now, &tm_buf);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &tm_buf);

    fprintf(log_file, "%s [%s] %s\n",timestamp,level_to_str(level),msg);
    fflush(log_file);

    pthread_mutex_unlock(&log_mutex);
}
