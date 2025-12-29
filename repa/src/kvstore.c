#include "kvstore.h"
#include "logger.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>


typedef struct kv_node {
    char *key;
    char *value;

    //0 - бессрочно
    time_t expire_at;
    //список для коллизий
    struct kv_node *next;
} kv_node;

static kv_node ** table= NULL;
static size_t table_size = 1024;
static int default_ttl = 0;

static size_t kv_hash(const char *key){
    unsigned long h = 5381;
    int c;
    while ((c = (unsigned char)*key++)) {
        h = ((h << 5) + h) + c;
    }
    return (size_t)(h % table_size);
}

int kv_init(int default_ttl_sec){
    default_ttl = default_ttl_sec;
    table = (kv_node**)calloc(table_size, sizeof(kv_node*));
    if (table == NULL) {
        LOG_ERROR("kv_init: calloc failed");
        return -1;
    }
    return 0;
}

void kv_shutdown(void){
    if (table == NULL) {
        return;
    }

    for (size_t i = 0; i < table_size; ++i) {
        kv_node *e = table[i];
        while (e != NULL) {
            kv_node *next = e->next;
            free(e->key);
            free(e->value);
            free(e);
            e = next;
        }
    }
    free(table);
    table= NULL;
}


int kv_check_and_delete_if_expired(size_t idx, kv_node *prev, kv_node *e){
    if (e== NULL) {
        return 0;
    }
    if (e->expire_at == 0) {
        return 0; 
    }

    time_t now = time(NULL);
    if (now >= e->expire_at) {
        if (prev!= NULL){
            prev->next = e->next;
        } 
        else{
            table[idx] = e->next;
        }

        free(e->key);
        free(e->value);
        free(e);
        return 1;
    }

    return 0;
}

int kv_set(const char *key, const char *value){
    if (table== NULL){
        return -1;
    }
    if (key == NULL|| value == NULL){
        return -1;
    }

    size_t idx = kv_hash(key);
    kv_node *e = table[idx];

    while (e) {
        if (strcmp(e->key, key) == 0) {
            free(e->value);
            size_t len = strlen(value) + 1;
            char *p = malloc(len);
            if (p == NULL){
                LOG_ERROR("kv_set failed");
                return -1;
            }
            memcpy(p, value, len);
            e->value = p;

            if (default_ttl > 0) {
                e->expire_at = time(NULL) + default_ttl;
            } else {
                e->expire_at = 0;
            }
            return 0;
        }
        e = e->next;
    }
    kv_node *new_e = (kv_node*)calloc(1, sizeof(kv_node));
    if (new_e == NULL) {
        LOG_ERROR("kv_set: calloc failed");
        return -1;
    }
    size_t len1 = strlen(key) + 1;
    char *p1 = malloc(len1 * sizeof(char));
    if (p1 == NULL){
        LOG_ERROR("kv_set failed");
        return -1;
    }
    memcpy(p1, key, len1);
    new_e->key = p1;

    size_t len2 = strlen(value) + 1;
    char *p2 = malloc(len2);
    if (p2 == NULL){
        LOG_ERROR("kv_set failed");
        return -1;
    }
    memcpy(p2, value, len2);
    new_e->value = p2;

    if (new_e->key == NULL || new_e->value == NULL) {
        LOG_ERROR("kv_set: pointer to null");
        free(new_e->key);
        free(new_e->value);
        free(new_e);
        return -1;
    }

    if (default_ttl > 0) {
        new_e->expire_at = time(NULL) + default_ttl;
    } else {
        new_e->expire_at = 0;
    }

    new_e->next = table[idx];
    table[idx] = new_e;
    return 0;
}

int kv_get(const char *key, char **out_value){
    if (table== NULL){
        return -1;
    }
    if (key == NULL || out_value == NULL) {
        return -1;
    }

    size_t idx = kv_hash(key);
    kv_node *e = table[idx];
    kv_node *prev = NULL;

    while (e) {
        if (strcmp(e->key, key) == 0) {
            if (kv_check_and_delete_if_expired(idx, prev, e) == 1) {
                return 0;
            }

            size_t len = strlen(e->value) + 1;
            char *p = malloc(len);
            if (p == NULL){
                LOG_ERROR("kv_get failed");
                return -1;
            }
            memcpy(p, e->value, len);

            *out_value = p;
            return 1;
        }
        prev = e;
        e = e->next;
    }
    return 0; 
}

int kv_del(const char *key){
    if (table== NULL || key == NULL){
        return 0;
    }

    size_t idx = kv_hash(key);
    kv_node *e = table[idx];
    kv_node *prev = NULL;

    while (e) {
        if (strcmp(e->key, key) == 0) {
            if (prev!= NULL) {
                prev->next = e->next;
            }
            else {
                table[idx] = e->next;
            }

            free(e->key);
            free(e->value);
            free(e);
            return 1;
        }
        prev = e;
        e = e->next;
    }
    return 0;
}

int kv_expire(const char *key, int timeout){
    if (table== NULL || key == NULL) {
        return 0;
    }
    if (timeout < 0) {
        return 0;
    }

    size_t idx = kv_hash(key);
    kv_node *e = table[idx];
    kv_node *prev = NULL;

    while (e) {
        if (strcmp(e->key, key) == 0) {
            if (kv_check_and_delete_if_expired(idx, prev, e) == 1) {
                return 0;
            }

            e->expire_at = time(NULL) + timeout;
            return 1;
        }
        prev = e;
        e = e->next;
    }

    return 0;
}

int kv_ttl(const char *key){
    if (table== NULL || key == NULL) {
        return -2;
    }

    size_t idx = kv_hash(key);
    kv_node *e = table[idx];
    kv_node *prev = NULL;

    while (e) {
        if (strcmp(e->key, key) == 0) {
            if (kv_check_and_delete_if_expired(idx, prev, e) == 1) {
                return -2;
            }

            if (e->expire_at == 0) {
                return -1; 
            }

            time_t now = time(NULL);

            return now;
        }
        prev = e;
        e = e->next;
    }

    return -2;
}
