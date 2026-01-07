#include "kvstore.h"
#include "logger.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>


typedef struct kv_node {
    char *key;
    char *value;
    time_t expire_at;
    struct kv_node *next;
} kv_node;

static kv_node ** table= NULL;
static size_t table_size = TABLE_SIZE;
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
        kv_node *node = table[i];
        while (node != NULL) {
            kv_node *next = node->next;
            free(node->key);
            free(node->value);
            free(node);
            node = next;
        }
    }
    free(table);
    table= NULL;
}


int kv_check_and_delete_if_expired(size_t idx, kv_node *prev, kv_node *node){
    if (node== NULL) {
        return 0;
    }
    if (node->expire_at == 0) {
        return 0; 
    }

    time_t now = time(NULL);
    if (now >= node->expire_at) {
        if (prev!= NULL){
            prev->next = node->next;
        } 
        else{
            table[idx] = node->next;
        }

        free(node->key);
        free(node->value);
        free(node);
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
    kv_node *node = table[idx];

    while (node) {
        if (strcmp(node->key, key) == 0) {
            free(node->value);
            size_t len = strlen(value) + 1;
            char *val_buf = malloc(len);
            if (val_buf == NULL){
                LOG_ERROR("kv_set failed");
                return -1;
            }
            memcpy(val_buf, value, len);
            node->value = val_buf;

            if (default_ttl > 0) {
                node->expire_at = time(NULL) + default_ttl;
            } else {
                node->expire_at = 0;
            }
            return 0;
        }
        node = node->next;
    }
    kv_node *new_node = (kv_node*)calloc(1, sizeof(kv_node));
    if (new_node == NULL) {
        LOG_ERROR("kv_set: calloc failed");
        return -1;
    }
    size_t len = strlen(key) + 1;
    char *key_buf = malloc(len * sizeof(char));
    if (key_buf == NULL){
        LOG_ERROR("kv_set failed");
        return -1;
    }
    memcpy(key_buf, key, len);
    new_node->key = key_buf;

    len = strlen(value) + 1;
    char *value_buf = malloc(len);
    if (value_buf == NULL){
        LOG_ERROR("kv_set failed");
        return -1;
    }
    memcpy(value_buf, value, len);
    new_node->value = value_buf;

    if (new_node->key == NULL || new_node->value == NULL) {
        LOG_ERROR("kv_set: pointer to null");
        free(new_node->key);
        free(new_node->value);
        free(new_node);
        return -1;
    }

    if (default_ttl > 0) {
        new_node->expire_at = time(NULL) + default_ttl;
    } else {
        new_node->expire_at = 0;
    }

    new_node->next = table[idx];
    table[idx] = new_node;
    return 0;
}


repa/client/repactl.c repa/include/auth.h repa/include/config.h repa/include/kvstore.h repa/include/logger.h
repa/include/resp.h repa/include/workqueue.h repa/src/auth.c repa/src/config.c repa/src/kvstore.c
repa/src/main.c repa/src/resp.c


int kv_get(const char *key, char **out_value){
    if (table== NULL){
        return -1;
    }
    if (key == NULL || out_value == NULL) {
        return -1;
    }

    size_t idx = kv_hash(key);
    kv_node *node = table[idx];
    kv_node *prev = NULL;

    while (node) {
        if (strcmp(node->key, key) == 0) {
            if (kv_check_and_delete_if_expired(idx, prev, node) == 1) {
                return 0;
            }

            size_t len = strlen(node->value) + 1;
            char *value_buf = malloc(len);
            if (value_buf == NULL){
                LOG_ERROR("kv_get failed");
                return -1;
            }
            memcpy(value_buf, node->value, len);

            *out_value = value_buf;
            return 1;
        }
        prev = node;
        node = node->next;
    }
    return 0; 
}

int kv_del(const char *key){
    if (table== NULL || key == NULL){
        return 0;
    }

    size_t idx = kv_hash(key);
    kv_node *node = table[idx];
    kv_node *prev = NULL;

    while (node) {
        if (strcmp(node->key, key) == 0) {
            if (prev!= NULL) {
                prev->next = node->next;
            }
            else {
                table[idx] = node->next;
            }

            free(node->key);
            free(node->value);
            free(node);
            return 1;
        }
        prev = node;
        node = node->next;
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
    kv_node *node = table[idx];
    kv_node *prev = NULL;

    while (node) {
        if (strcmp(node->key, key) == 0) {
            if (kv_check_and_delete_if_expired(idx, prev, node) == 1) {
                return 0;
            }

            node->expire_at = time(NULL) + timeout;
            return 1;
        }
        prev = node;
        node = node->next;
    }

    return 0;
}

int kv_ttl(const char *key){
    if (table== NULL || key == NULL) {
        return -2;
    }

    size_t idx = kv_hash(key);
    kv_node *node = table[idx];
    kv_node *prev = NULL;

    while (node) {
        if (strcmp(node->key, key) == 0) {
            if (kv_check_and_delete_if_expired(idx, prev, node) == 1) {
                return -2;
            }

            if (node->expire_at == 0) {
                return -1; 
            }

            time_t now = time(NULL);

            return now;
        }
        prev = node;
        node = node->next;
    }

    return -2;
}
