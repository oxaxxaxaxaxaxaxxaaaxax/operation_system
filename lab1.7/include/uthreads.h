#pragma once

#include <ucontext.h>

#define THREAD_COUNT 5

typedef enum{
    UT_OK = 0,
    UT_EINVAL =1,
    UT_EAGAIN =2,
    UT_ESRCH =3
} uthread_err;

typedef struct thread_attr thread_attr;

typedef struct cleanup_handlers_node{
    void (*cleanup_routine) (void*);
    void* arg;
    struct cleanup_handlers_node *next;
}cleanup_handlers_node;

typedef struct stack_cleanup_handlers{
    cleanup_handlers_node* top;
}stack_cleanup_handlers;


typedef struct uthread{
    ucontext_t ctx;
    int tid;
    int is_created;
    int is_terminated;
    int is_detached;
    int is_cancel_pending;
    void *stack;
    thread_attr *thread_attr;
    stack_cleanup_handlers *cl_handlers;
    void *map_region;
} uthread;

/* Create new thread */
int uthread_create(uthread **out_thread, void *(*start_fn)(void *), void *arg);

/* Start cooperative scheduler */
void uthreads_schedule(void);

/* Yield control to another thread */
int uthread_yield(void);

/* Exit current thread */
void uthread_exit(void *retval);

/* Get current thread */
uthread *uthread_self(void);

/* Compare threads */
int uthread_equal(uthread *a, uthread *b);

/* Wait for thread to finish */
int uthread_join(uthread *t, void **retval);

/* Detach thread */
int uthread_detach(uthread *t);

/* Request thread cancel */
int uthread_cancel(uthread *t);

/* Check if thread was canceled */
void uthread_testcancel(void);

/* Push cleanup handler */
void uthread_cleanup_push(void (*cleanup_routine) (void*), void *arg);

/* Pop cleanup handler */
void uthread_cleanup_pop(int execute);

/* Init main context*/
void init_main_ctx();