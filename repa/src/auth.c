#include <string.h>
#include "auth.h"
#include "constants.h"

static char user[REPA_MAX_USER_LEN];
static char password[REPA_MAX_PASS_LEN];


int auth_init(const char *username, const char *pass) {
    if (username == NULL || pass == NULL){
        return -1;
    }
    strncpy(user, username, REPA_MAX_USER_LEN);
    strncpy(password, pass, REPA_MAX_PASS_LEN);
    return 0;
}

int auth_check(const char *username, const char *pass) {
    if (username == NULL || pass == NULL){
        return 0;
    }
    if (strcmp(username, user) != 0) {
        return 0;
    }
    if (strcmp(pass, password) != 0) {
        return 0;
    }
    return 1;
}
