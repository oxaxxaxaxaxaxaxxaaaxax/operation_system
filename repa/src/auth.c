#include "auth.h"
#include <string.h>

static char user[AUTH_MAX_USER];
static char password[AUTH_MAX_PASS];


int auth_init(const char *_user, const char *_pass) {
    if (_user == NULL || _pass == NULL){
        return -1;
    }
    strncpy(user, _user, AUTH_MAX_USER);
    strncpy(password, _pass, AUTH_MAX_PASS);
    return 0;
}

int auth_check(const char *_user, const char *_pass) {
    if (_user == NULL || _pass == NULL){
        return 0;
    }
    if (strcmp(_user, user) != 0) {
        return 0;
    }
    if (strcmp(_pass, password) != 0) {
        return 0;
    }
    return 1;
}
