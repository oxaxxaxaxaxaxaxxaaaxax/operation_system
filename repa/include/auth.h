#pragma once

int auth_init(const char *username, const char *password);

int auth_check(const char *username, const char *password);
