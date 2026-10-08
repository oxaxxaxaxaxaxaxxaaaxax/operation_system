#pragma once

#include <pthread.h>
#include <stdbool.h>
#include "resp.h"

struct client_ctx;

typedef struct task {
    struct client_ctx *ctx;
    resp_command cmd;
    struct task *next;
} task;

typedef struct work_queue {
    task *head;
    task *tail;
    size_t size;

    bool shutdown;

    pthread_mutex_t mtx;
    pthread_cond_t  cv;
} work_queue;

void wq_init(work_queue *q);

void wq_destroy(work_queue *q);

int wq_push(work_queue *q, task *t);

task* wq_pop(work_queue *q);

void wq_shutdown(work_queue *q);
