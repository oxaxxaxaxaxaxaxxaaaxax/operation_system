#pragma once

#include <time.h>


int kv_init(int default_ttl_sec);

void kv_shutdown(void);

int kv_set(const char *key, const char *value);

int kv_get(const char *key, char **out_value);

int kv_del(const char *key);

int kv_expire(const char *key, int seconds);

int kv_ttl(const char *key);
