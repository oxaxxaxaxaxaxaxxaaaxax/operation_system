#pragma once

#include <stddef.h>

typedef struct {
    int argc;
    char **argv;
} resp_command;

typedef struct {
    char  data[4096];
    size_t len;
} resp_buffer;

void resp_buffer_init(resp_buffer *buf);

int resp_buffer_append(resp_buffer *buf, const char *us_data, size_t n);

int resp_try_parse_command(resp_buffer *buf, resp_command *out_cmd);

void resp_free_command(resp_command *cmd);

int resp_send_simple_string(int fd, const char *s);

int resp_send_error(int fd, const char *msg);

int resp_send_bulk_string(int fd, const char *s);

int resp_send_integer(int fd, long long value);

int resp_send_null_bulk(int fd);

