#include "workqueue.h"

#include <stdlib.h>

void wq_init(work_queue *q) {
    q->head = NULL;
    q->tail = NULL;
    q->shutdown = 0;

    pthread_mutex_init(&q->mtx, NULL);
    pthread_cond_init(&q->cv, NULL);
}

void wq_shutdown(work_queue *q) {
    pthread_mutex_lock(&q->mtx);
    q->shutdown = 1;
    pthread_cond_broadcast(&q->cv);
    pthread_mutex_unlock(&q->mtx);
}

void wq_destroy(work_queue *q) {
    pthread_mutex_destroy(&q->mtx);
    pthread_cond_destroy(&q->cv);
}

int wq_push(work_queue *q, task *t) {
    pthread_mutex_lock(&q->mtx);

    if (q->shutdown) {
        pthread_mutex_unlock(&q->mtx);
        return -1;
    }

    t->next = NULL;
    if (q->tail == NULL) {
        q->head = q->tail = t;
    } else {
        q->tail->next = t;
        q->tail = t;
    }

    pthread_cond_signal(&q->cv);
    pthread_mutex_unlock(&q->mtx);
    return 0;
}

task *wq_pop(work_queue *q) {
    pthread_mutex_lock(&q->mtx);

    while (!q->shutdown && q->head == NULL) {
        pthread_cond_wait(&q->cv, &q->mtx);
    }

    if (q->head == NULL) {
        pthread_mutex_unlock(&q->mtx);
        return NULL;
    }

    task *t = q->head;
    q->head = t->next;
    if (q->head == NULL){
        q->tail = NULL;
    } 

    pthread_mutex_unlock(&q->mtx);
    return t;
}
