#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include "threadpoolPrivate.h"

typedef struct runnable {
  void (*function)(void *, size_t);
  void *arg;
  size_t argsz;
} runnable_t;


typedef struct thread_pool {
    size_t pool_size;
    job_distributor* distributor;
    pthread_t* threads;

} thread_pool_t;

typedef struct thread_pool thread_pool_t;

int thread_pool_init(thread_pool_t *pool, size_t pool_size);

void thread_pool_destroy(thread_pool_t *pool);

int defer(thread_pool_t *pool, runnable_t runnable);

#endif
