#include "resp.h"
#include "logger.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <ctype.h>

void resp_buffer_init(resp_buffer *buf){
    buf->len = 0;
    return;
}

int resp_buffer_append(resp_buffer *buf, const char *us_data, size_t n){
    if (n == 0) return 0;
    if (buf->len + n > sizeof(buf->data)) {
        return -1;
    }
    memcpy(buf->data + buf->len, us_data, n);
    buf->len += n;
    return 0;
}

static int find_rn(const char *data, size_t len, size_t start){
    for (size_t i = start; i + 1 < len; i++) {
        if (data[i] == '\r' && data[i+1] == '\n') {
            return (int)i;
        }
    }
    return -1;
}


static int parse_number(const char *data, size_t len, size_t *pos, long long *out_val){
    if (*pos >= len) {
        return 0;
    }

    int rn_pos = find_rn(data, len, *pos);
    if (rn_pos < 0) {
        return 0;
    }

    int i = (int)*pos;
    int end = rn_pos;
    int neg = 0;
    long long value = 0;

    if (i >= end) {
        return -1;
    }

    if (data[i] == '-') {
        neg = 1;
        i++;
        if (i >= end) {
            return -1;
        }
    }

    while(i < end) {
        if (!isdigit((unsigned char)data[i])) {
            return -1;
        }
        value = value * 10 + (data[i] - '0');
        i++;
    }

    value = neg ? -value : value;

    *out_val = value;
    *pos = (size_t)(rn_pos + 2);  
    return 1;
}

int resp_try_parse_command(resp_buffer *buf, resp_command *out_cmd){
    if (buf->len == 0) {
        return 0;
    }

    size_t pos = 0;
    char *data = buf->data;
    size_t len = buf->len;

    if (data[pos] != '*') {
        LOG_ERROR("RESP: expected array '*', got something else");
        return -1;
    }
    pos++;

    long long count = 0;
    int r = parse_number(data, len, &pos, &count);
    if (r == 0) {
        return 0;
    }
    if (r < 0 || count < 0) {
        LOG_ERROR("RESP: invalid array length");
        return -1;
    }
    if (count == 0) {
        LOG_ERROR("RESP: empty array");
        return -1;
    }

    char **argv = malloc((size_t)count * sizeof(char*));
    if (argv == NULL) {
        LOG_DEBUG("RESP: malloc failed");
        return -1;
    }
    for (int i = 0; i < count; ++i) {
        if (pos >= len) {
            for (int j = 0; j < i; j++){
                free(argv[j]);
            }
            free(argv);
            return 0;
        }

        if (data[pos] != '$') {
            LOG_ERROR("RESP: expected bulk string '$'");
            for (int j = 0; j < i; j++){
                free(argv[j]);
            }
            free(argv);
            return -1;
        }
        pos++;

        long long bulk_len = 0;
        r = parse_number(data, len, &pos, &bulk_len);
        if (r == 0) {
            for (int j = 0; j < i; j++){
                free(argv[j]);
            }
            free(argv);
            return 0;
        }
        if (r < 0 || bulk_len < 0) {
            LOG_ERROR("RESP: invalid bulk length");
            for (int j = 0; j < i; j++){
                free(argv[j]);
            }
            free(argv);
            return -1;
        }

        if (pos + (size_t)bulk_len + 2 > len) {
            for (int j = 0; j < i; j++){
                free(argv[j]);
            }
            free(argv);
            return 0;
        }
        char *s = (char*)malloc((size_t)bulk_len + 1);
        if (s == NULL) {
            LOG_ERROR("RESP: malloc failed");
            int j;
            for (j = 0; j < i; ++j) free(argv[j]);
            free(argv);
            return -1;
        }
        memcpy(s, data + pos, (size_t)bulk_len);
        s[bulk_len] = '\0';
        argv[i] = s;

        pos += (size_t)bulk_len;

        if (pos + 1 >= len || data[pos] != '\r' || data[pos+1] != '\n') {
            LOG_ERROR("RESP: missing CRLF after bulk string");
            int j;
            for (j = 0; j <= i; ++j) free(argv[j]);
            free(argv);
            return -1;
        }
        pos += 2;
    }

    out_cmd->argc = (int)count;
    out_cmd->argv = argv;

    if (pos < len) {
        memmove(data, data + pos, len - pos);
    }
    buf->len = len - pos;

    return 1;
}

void resp_free_command(resp_command *cmd){
    if (cmd == NULL){
        return;
    } 
    for (int i = 0; i < cmd->argc; ++i) {
        free(cmd->argv[i]);
    }
    free(cmd->argv);
    cmd->argv = NULL;
    cmd->argc = 0;
}

int resp_send_simple_string(int fd, const char *s){
    char buf[BUFFER_SIZE];
    int n = snprintf(buf, sizeof(buf), "+%s\r\n", s);
    if (n < 0) {
        return -1;
    }
    if (write(fd, buf, (size_t)n) < 0) {
        return -1;
    }
    return 0;
}

int resp_send_error(int fd, const char *msg){
    char buf[BUFFER_SIZE];
    int n = snprintf(buf, sizeof(buf), "-ERR %s\r\n", msg);
    if (n < 0) {
        return -1;
    }
    if (write(fd, buf, (size_t)n) < 0) {
        return -1;
    }
    return 0;
}

int resp_send_bulk_string(int fd, const char *s){
    size_t len = strlen(s);
    char header[HEADER_SIZE];
    int n = snprintf(header, sizeof(header), "$%zu\r\n", len);
    if (n < 0) {
        return -1;
    }
    if (write(fd, header, (size_t)n) < 0) {
        return -1;
    }
    if (write(fd, s, len) < 0) {
        return -1;
    }
    if (write(fd, "\r\n", 2) < 0) {
        return -1;
    }
    return 0;
}

int resp_send_integer(int fd, long long value){
    char buf[VALUE_SIZE];
    int n = snprintf(buf, sizeof(buf), ":%lld\r\n", value);
    if (n < 0){
        return -1;
    } 
    if (write(fd, buf, (size_t)n) < 0) {
        return -1;
    }
    return 0;
}

int resp_send_null_bulk(int fd){
    const char *resp = "$-1\r\n";
    size_t len = strlen(resp);

    if (write(fd, resp, len) < 0) {
        return -1;
    }
    return 0;
}
