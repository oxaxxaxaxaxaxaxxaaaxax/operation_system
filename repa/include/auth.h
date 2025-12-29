#pragma once

#define AUTH_MAX_USER 64
#define AUTH_MAX_PASS 64

int auth_init(const char *username, const char *password);

int auth_check(const char *username, const char *password);

