#include "future.h"
#include "threadpool.h"
#include "err.h"
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>


enum error {
    malloc_failed = -1
};

typedef void *(*function_t)(void *);

typedef struct input {
    future_t* fut;
    callable_t call;
} input_t;

void initFuture(future_t* future) {
    int err;

    if ((err = pthread_mutex_init(&future->lock, NULL)) != 0)
        syserr(err, "failed mutex init");
    if ((err = pthread_cond_init(&future->await, NULL)) != 0)
        syserr(err, "failed cond init");

    future->solution = NULL;
    future->mappedFut = NULL;
    future->isMapped = false;
    future->mappedPool = NULL;
}

void* fun_wrapper(void *arg, size_t argsz);

runnable_t makeRunnable(callable_t callable, future_t* future) {
    runnable_t run;

    run.argsz = callable.argsz;
    input_t* in = (input_t*)malloc(sizeof(input_t));
    if (!in)
        return run;

    in->fut = future;
    in->call = callable;
    run.arg = in;
    run.function = (void *)fun_wrapper;

    return run;
}

// Wraps a function in arg so it signals when it finishes computing.
void* fun_wrapper(void *arg, size_t argsz) {
    int err;

    input_t* in = (input_t*)arg;
    callable_t call = in->call;
    future_t* fut = in->fut;

    size_t* argP = (size_t*)malloc(argsz);
    if (!argP)
        syserr(malloc_failed, "malloc failed");

    if ((err = pthread_mutex_lock(&fut->lock)) != 0)
        syserr(err, "mutex lock in fun_wrapper failed");

    void* temp = call.function(call.arg, call.argsz, argP);
    fut->solution = temp;

    free(argP);
    if (fut->isMapped) {
        fut->mappedCall.arg = temp;
        fut->mappedCall.argsz = sizeof(temp);

        runnable_t run = makeRunnable(fut->mappedCall, fut->mappedFut);
        if ((err = defer(fut->mappedPool, run) != 0))
            syserr(err, "defer map to pool failed");
    }

    free(in);
    if ((err = pthread_cond_signal(&fut->await)) != 0)
        syserr (err, "cond signal failed");

    if ((err = pthread_mutex_unlock(&fut->lock)) != 0)
        syserr(err, "mutex unlock failed");

    return NULL;
}

int async(thread_pool_t *pool, future_t *future, callable_t callable) {

    initFuture(future);

    runnable_t run = makeRunnable(callable, future);
    if (!run.arg)
        return malloc_failed;

    return defer(pool, run);
}

void *await(future_t *future) {
    int err;

    if ((err = pthread_mutex_lock(&future->lock)) != 0)
        syserr(err, "await lock failed");
    if (!future->solution) {
        if ((err = pthread_cond_wait(&future->await, &future->lock)) != 0)
            syserr(err, "cond wait failed");
    }
    if ((err = pthread_mutex_unlock(&future->lock)) != 0)
        syserr(err, "await unlock failed");

    return future->solution;
}

int map(thread_pool_t *pool, future_t *future, future_t *from,
        void *(*function)(void *, size_t, size_t *)) {
    int err;

    if ((err = pthread_mutex_lock(&from->lock)) != 0)
        syserr(err, "mutex lock in map failed");

    initFuture(future);

    if (from->solution) {
        callable_t call;
        call.arg = from->solution;
        call.argsz = sizeof(from->solution);
        call.function = function;
        runnable_t run = makeRunnable(call, future);
        if (!run.arg) {
            if ((err = pthread_mutex_unlock(&from->lock)) != 0)
                syserr(err, "mutex unlock failed");

            return malloc_failed;
        }

        err = defer(pool, run);

        int ret = err;
        if ((err = pthread_mutex_unlock(&from->lock)) != 0)
            syserr(err, "mutex unlock failed");

        return ret;
    } else {
        from->isMapped = true;
        from->mappedFut = future;
        from->mappedPool = pool;
        callable_t call;
        call.function = function;
        from->mappedCall = call;
    }

    if ((err = pthread_mutex_unlock(&from->lock)) != 0)
        syserr(err, "mutex unlock failed");

    return 0;
}