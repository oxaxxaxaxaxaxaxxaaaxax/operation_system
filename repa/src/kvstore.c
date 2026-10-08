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
static size_t table_items = 0;

static size_t kv_hash(const char *key, const size_t table_size){
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
    table_items = 0;
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

        table_items--;
        free(node->key);
        free(node->value);
        free(node);
        return 1;
    }

    return 0;
}

int kv_resize(size_t new_size){
    LOG_INFO("Resize store");
    kv_node **new_table = calloc(new_size, sizeof(*new_table));
    if (new_table == NULL) {
        return -1;
    }

    for (size_t i = 0; i < table_size; i++) {
        kv_node *node = table[i];
        while (node != NULL) {
            kv_node *next = node->next;

            size_t new_idx = kv_hash(node->key, new_size);
            node->next = new_table[new_idx];
            new_table[new_idx] = node;

            node = next;
        }
    }

    free(table);
    table = new_table;
    table_size = new_size;
    return 0;
}

int kv_set(const char *key,const char *value){
    if (table== NULL){
        return -1;
    }
    if (key == NULL|| value == NULL){
        return -1;
    }

    if (table_items + 1 > table_size * MAX_LOAD_FACTOR) {
        int res = kv_resize(table_size * 2);
        if (res != 0) {
            return -1;
        }
    }

    size_t idx = kv_hash(key, table_size);
    kv_node *node = table[idx];

    while (node != NULL) {
        if (strcmp(node->key, key) == 0) {
            free(node->value);

            size_t len = strlen(value) + 1;
            char *val_buf = malloc(len);
            if (val_buf == NULL) {
                LOG_ERROR("kv_set failed");
                return -1;
            }

            memcpy(val_buf, value, len);
            node->value = val_buf;
            node->expire_at = (default_ttl > 0) ? time(NULL) + default_ttl : 0;
            return 0;
        }
        node = node->next;
    }

    kv_node *new_node = calloc(1, sizeof(*new_node));
    if (new_node == NULL) {
        LOG_ERROR("kv_set: calloc failed");
        return -1;
    }

    size_t len = strlen(key) + 1;
    new_node->key = malloc(len);
    if (new_node->key == NULL) {
        free(new_node);
        return -1;
    }
    memcpy(new_node->key, key, len);

    len = strlen(value) + 1;
    new_node->value = malloc(len);
    if (new_node->value == NULL) {
        free(new_node->key);
        free(new_node);
        return -1;
    }
    memcpy(new_node->value, value, len);

    new_node->expire_at = (default_ttl > 0) ? time(NULL) + default_ttl : 0;

    new_node->next = table[idx];
    table[idx] = new_node;

    table_items++;
    return 0;
}


int kv_get(const char *key, char **out_value){
    if (table== NULL){
        return -1;
    }
    if (key == NULL || out_value == NULL) {
        return -1;
    }

    size_t idx = kv_hash(key, table_size);
    kv_node *node = table[idx];
    kv_node *prev = NULL;

    while (node != NULL) {
        if (strcmp(node->key, key) == 0) {
            if (kv_check_and_delete_if_expired(idx, prev, node) == 1) {
                return 0;
            }

            size_t len = strlen(node->value) + 1;
            char *value_buf = malloc(len);
            if (value_buf == NULL) {
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

    size_t idx = kv_hash(key, table_size);
    kv_node *node = table[idx];
    kv_node *prev = NULL;

    while (node != NULL) {
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
            table_items--;
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

    size_t idx = kv_hash(key, table_size);
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

    size_t idx = kv_hash(key, table_size);
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
            time_t ttl = node->expire_at - now;
            if (ttl < 0) {
                ttl = 0;
            }
            return (int)ttl;
        }
        prev = node;
        node = node->next;
    }

    return -2;
}
