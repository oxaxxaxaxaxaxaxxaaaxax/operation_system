#define _GNU_SOURCE
#include "uthreads.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#define STACK_SIZE 8*1024 * 1024
#define GUARD_SIZE 8*1024 * 1024
#define NO_THREAD -1

typedef struct thread_attr {
    void *(*start_routine)(void *);
    void *arg;
    void *retval;
}thread_attr;

static uthread table_threads[THREAD_COUNT];
static ucontext_t main_ctx;
static int curr_thread_idx = -1;

//варианты избегать блокирующие системные вызовы
//посмотреть goroutine (вариант решения)
//если что сделать отображение mxn 
//как протестить блокирующий сигнал(ответ - большой слип)

void init_main_ctx(){
    getcontext(&main_ctx);
}

int find_next_thread_idx(){
    if(curr_thread_idx == -1){
        return -1;
    }
    int old_thread_idx = curr_thread_idx;
    int next_thread_idx = (curr_thread_idx + 1)%THREAD_COUNT;
    while(old_thread_idx != next_thread_idx){
        if(table_threads[next_thread_idx].is_created && !table_threads[next_thread_idx].is_terminated){
            return next_thread_idx;
        }
        next_thread_idx = (next_thread_idx+1)%THREAD_COUNT;
    }
    //если эо вызвано из yield и текущий еще жив то возвразается в него
    if(table_threads[old_thread_idx].is_created && !table_threads[old_thread_idx].is_terminated){
        return old_thread_idx;
    }
    return -1;
}

void schedule_next(){
    int next_thread_idx = find_next_thread_idx();
    if (next_thread_idx ==-1){
        setcontext(&main_ctx);
        return;
    }
    if(next_thread_idx == curr_thread_idx){
        return;
    }
    ucontext_t* oldctx = &table_threads[curr_thread_idx].ctx;
    curr_thread_idx = next_thread_idx;
    swapcontext(oldctx, &table_threads[next_thread_idx].ctx);
    return;
}

uthread* uthread_self(){
    if(curr_thread_idx ==-1){
        return NULL;
    }
    return &table_threads[curr_thread_idx];
}

void run_cleanup(){
    uthread *t =  uthread_self();
    if(t == NULL){
        return;
    }
    if(t->cl_handlers == NULL){
        return;
    }
    while(t->cl_handlers->top != NULL){
        cleanup_handlers_node* h_node = t->cl_handlers->top;
        t->cl_handlers->top = h_node->next;
        if(h_node->cleanup_routine == NULL){
            free(h_node);
            h_node = NULL;
            return;
        }
        h_node->cleanup_routine(h_node->arg);
        free(h_node);
        h_node = NULL;
    }
}

void start_func(int idx){
    uthread *thread = &table_threads[idx];
    thread->thread_attr->retval = thread->thread_attr->start_routine(thread->thread_attr->arg);
    run_cleanup();
    if(thread->is_detached){
        uthread_exit((void*)0);
    }
    thread->is_terminated = 1;
    schedule_next();
}

void uthread_exit(void *retval){
    //in my realization tid = idx in []table_threads
    uthread *t =  uthread_self();
    if(t == NULL){
        setcontext(&main_ctx);
    }
    t->is_created = 0;
    t->thread_attr->retval = retval;
    run_cleanup();
    t->is_terminated =1;
    if(t->is_detached){
        t->is_created =0;
        if(t->stack != NULL){
            free(t->stack);
            t-> stack = NULL;
        }
        t->thread_attr->start_routine = NULL;
        if(t->thread_attr){
            free(t->thread_attr);
            t->thread_attr = NULL;
        }
        if(t->cl_handlers){
            free(t->cl_handlers);
            t->cl_handlers = NULL;
        }
    }
    schedule_next();
}

void uthreads_schedule(){
    for(int i=0;i < THREAD_COUNT;i++){
        if(table_threads[i].is_created && !table_threads[i].is_terminated){
            curr_thread_idx =i;
            swapcontext(&main_ctx, &table_threads[i].ctx);
            return;
        }
    }
}

int uthread_yield(){
    if(curr_thread_idx ==-1){
        return UT_OK;
    }
    int next_thread_idx = find_next_thread_idx();
    if(next_thread_idx == -1 || next_thread_idx == curr_thread_idx){
        return UT_OK;
    }
    ucontext_t* oldctx = &table_threads[curr_thread_idx].ctx;
    curr_thread_idx = next_thread_idx;
    swapcontext(oldctx, &table_threads[next_thread_idx].ctx);
    return UT_OK;
}

int alloc_guarded_pages_stack(uthread* t,void ** stack_top, void **available_stack_bottom){
    void *reg = mmap(NULL, GUARD_SIZE+ STACK_SIZE, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(reg == MAP_FAILED){
        return -1;
    }
    t->map_region = reg;
    if(mprotect(reg, GUARD_SIZE, PROT_NONE) == -1){
        munmap(reg, GUARD_SIZE+ STACK_SIZE);
        return -1;
    }
    *stack_top = (char*)reg +STACK_SIZE + GUARD_SIZE;
    *available_stack_bottom = (char*)reg + GUARD_SIZE;
    return 0;
}

int uthread_create(uthread** thread, void *(*start_routine)(void *), void* arg){
    if(thread == NULL){
        return UT_EINVAL;
    }
    if(start_routine == NULL){
        return UT_EINVAL;
    }
    int idx = -1;
    for(int i =0;i< THREAD_COUNT;i++){
        if(!table_threads[i].is_created){
            idx = i;
            break;
        }
    }
    if(idx == -1){
        return UT_EAGAIN;
    }
    uthread* t = &table_threads[idx];
    t->thread_attr = malloc(sizeof(thread_attr));
    t->thread_attr->retval = NULL;
    t->tid = idx;
    t->is_terminated =0;
    t->is_detached=0;
    t->is_cancel_pending =0;
    t->cl_handlers = malloc(sizeof(stack_cleanup_handlers));
    if(t->cl_handlers == NULL){
        free(t->thread_attr);
        return UT_EAGAIN;
    }
    t->cl_handlers->top = NULL;
    t->thread_attr->arg = arg;
    t->thread_attr->start_routine = start_routine;

    void * bott_available_stack;
    void * top_stack;
    if(alloc_guarded_pages_stack(t,&top_stack, &bott_available_stack) == -1){
        free(t->thread_attr);
        free(t->cl_handlers);
        return UT_EAGAIN;
    }

    t->stack = bott_available_stack;
    //t->map_region = top_stack;
    if(t->stack == NULL){
        free(t->thread_attr);
        t->is_created =0;
        free(t->cl_handlers);
        t->cl_handlers=NULL;
        return UT_EAGAIN;
    }

    getcontext(&t->ctx);
    t->ctx.uc_stack.ss_size = STACK_SIZE;
    t->ctx.uc_stack.ss_sp = t->stack;
    t->ctx.uc_link = &main_ctx;

    makecontext(&t->ctx, (void (*)(void))start_func, 1, idx);
    t->is_created =1;
    *thread = t;
    return UT_OK;
}

int uthread_join(uthread *thread, void **retval){
    if(thread == NULL){
        return UT_EINVAL;
    }
    if(thread->is_detached){
        return UT_EINVAL;
    }
    while(!thread->is_terminated){
        uthreads_schedule();
    }
    if(retval!= NULL){
        *retval = thread->thread_attr->retval;
    }
    thread->is_created = 0;
    if(thread->stack != NULL){
        munmap(thread->map_region, GUARD_SIZE+ STACK_SIZE);
    }
    thread->thread_attr->start_routine = NULL;
    if(thread->thread_attr){
        free(thread->thread_attr);
        thread->thread_attr = NULL;
    }
    if(thread->cl_handlers){
        free(thread->cl_handlers);
        thread->cl_handlers = NULL;
    }
    return UT_OK;
}

int uthread_detach(uthread* t){
    if(t == NULL){
        return UT_EINVAL;
    }
    if(t->is_terminated){
        t->is_created =0;
        if(t->stack != NULL){
            munmap(t->map_region, GUARD_SIZE+ STACK_SIZE);
        }
        t->thread_attr->start_routine = NULL;
        if(t->thread_attr){
            free(t->thread_attr);
            t->thread_attr = NULL;
        }
        if(t->cl_handlers){
            free(t->cl_handlers);
            t->cl_handlers = NULL;
        }
        return UT_OK;
    }
    t->is_detached =1;
    return UT_OK;
}

int uthread_cancel(uthread* t){
    if(t == NULL){
        return UT_ESRCH;
    }
    if(!t->is_created){
        return UT_ESRCH;
    }
    if(t->is_terminated){
        return UT_ESRCH;
    }
    t->is_cancel_pending=1;
    return UT_OK;
}

int uthread_equal(uthread *t1, uthread *t2){
    return (t1->tid == t2->tid) ? 1: 0;
}


void uthread_testcancel(){
    uthread *t =  uthread_self();
    if(t != NULL && t->is_cancel_pending){
        uthread_exit((void*)0);
    }
}

void uthread_cleanup_push(void (*cleanup_routine) (void*), void* arg){
    uthread *t =  uthread_self();
    if(t == NULL){
        return;
    }
    if(cleanup_routine == NULL){
        return;
    }
    cleanup_handlers_node* h_node = t-> cl_handlers->top;
    h_node = malloc(sizeof(cleanup_handlers_node));
    if(h_node == NULL){
        return;
    }
    h_node->cleanup_routine = cleanup_routine;
    h_node->arg = arg;
    h_node->next = t->cl_handlers->top;
    t->cl_handlers->top = h_node;
}

void uthread_cleanup_pop(int execute){
    uthread *t =  uthread_self();
    if(t == NULL){
        return;
    }
    if(t->cl_handlers->top == NULL){
        return;
    }
    cleanup_handlers_node* h_node = t->cl_handlers->top;
    t->cl_handlers->top = h_node->next;
    if(h_node->cleanup_routine == NULL){
        free(h_node);
        h_node = NULL;
        return;
    }
    if(!execute){
        free(h_node);
        h_node = NULL;
        return;
    }
    h_node->cleanup_routine(h_node->arg);
    free(h_node);
    h_node = NULL;
}
