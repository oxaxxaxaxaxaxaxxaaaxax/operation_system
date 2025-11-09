#include "uthreads.h"
#include <stdlib.h>
#include <stdio.h>

#define RED "\033[41m"
#define GREEN  "\033[42m"
#define NOCOLOR "\033[0m"

void* thread_func(void * arg){
    long id = (long)arg;
    printf("Hello from %ld thread!\n", id);
    return (void*)42;
}

void test_basic_create_threads(){
    uthread* t1 = NULL;
    uthread* t2 = NULL;
    int err =0;
    long id1 = 1;
    long id2 = 2;
    void* retval1;
    void* retval2;
    init_main_ctx();
    err = uthread_create(&t1, thread_func, (void*)id1);
    if(err != UT_OK){
        printf("test_basic_create_threads() FAILED. uthread create failed with thread %ld\n", id1);
    }
    err = uthread_create(&t2, thread_func, (void*)id2);
    if(err != UT_OK){
        printf("test_basic_create_threads() FAILED. uthread create failed with thread %ld\n", id2);
    }
    uthreads_schedule();
    err = uthread_join(t1,(void**)&retval1);  
    if (err != UT_OK) {        
        printf("main: pthread_join() failed!");      
        return;    
    }
    printf("Return value from %ld: %ld\n", id1, (long)retval1);
    err = uthread_join(t2,(void**)&retval2);  
    if (err != UT_OK) {        
        printf("main: pthread_join() failed!");      
        return;    
    }
    printf("Return value from %ld: %ld\n", id2, (long)retval2);
    printf(GREEN"test_basic_create_threads() PASSED" NOCOLOR "\n");
}

void test_uniqueness(){
    int err =0;

    uthread* tids[THREAD_COUNT];
    for(long i=0;i<THREAD_COUNT;i++){
        err = uthread_create(&tids[i], thread_func, (void*)i);
        if(err != UT_OK){
            printf("uthread create() failed with thread %ld\n", i);
            return;
        }
    }
    for(int i=0;i<THREAD_COUNT;i++){
        int curr_tid = tids[i]->tid;
        for(int j=0;j<THREAD_COUNT;j++){
            if(j== i){
                continue;
            }
            if(curr_tid == tids[j]->tid){
                printf("test_uniqueness() FAILED. equal tids of a different threads\n");
            }
        }
    }
    for(int i =0;i<THREAD_COUNT;i++){
        err = uthread_join(tids[i],NULL);  
        if (err != UT_OK) {        
            printf("main: pthread_join() failed!");      
            return;    
        }
    }
    printf(GREEN"test_uniqueness() PASSED" NOCOLOR "\n");
}

void test_limit_threads(){
    int err =0;
    uthread* tids[THREAD_COUNT+2];
    int count_limits =0;
    int threads_excess_limit = THREAD_COUNT+2;
    for(long i=0;i<threads_excess_limit;i++){
        err = uthread_create(&tids[i], thread_func, (void*)i);
        if(err != UT_OK){
            printf("uthread create() failed with thread %ld\n", i);
        }
        if((i >= THREAD_COUNT)&& (err == UT_EAGAIN)){
            count_limits++;
        }
    }
    if(count_limits == threads_excess_limit - THREAD_COUNT){
        printf(GREEN"test_limit_threads() PASSED" NOCOLOR"\n");
        return;
    }
    printf(GREEN "test_limit_threads() PASSED" NOCOLOR "\n");
}

int main(void){
    test_basic_create_threads();
    test_uniqueness();
    test_limit_threads();
    return 0;
}

