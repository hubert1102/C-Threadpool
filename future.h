#ifndef FUTURE_H
#define FUTURE_H

#include "threadpool.h"
#include<pthread.h>

typedef struct callable {
  void *(*function)(void *, size_t, size_t *);
  void *arg;
  size_t argsz;
} callable_t;

typedef struct future future_t;
typedef struct future {
    pthread_cond_t await;
    pthread_mutex_t lock;
    void* solution;
    bool isMapped;
    future_t* mappedFut;
    callable_t mappedCall;
    thread_pool_t* mappedPool;

} future_t;




int async(thread_pool_t *pool, future_t *future, callable_t callable);

int map(thread_pool_t *pool, future_t *future, future_t *from,
        void *(*function)(void *, size_t, size_t *));

void *await(future_t *future);

#endif
