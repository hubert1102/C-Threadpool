#ifndef ASYNC_THREADPOOLPRIVATE_H
#define ASYNC_THREADPOOLPRIVATE_H

//#include "threadpool.h"

typedef struct runnable runnable_t;

typedef struct job_list job_list;

// Monitor
typedef struct job_distributor {
    pthread_mutex_t lock;
    pthread_cond_t available_jobs;
    pthread_cond_t shutdown_wait;
    int threads_waiting;
    int threads_working;
    bool shutdown;

    job_list* jobs;
} job_distributor;


#endif //ASYNC_THREADPOOLPRIVATE_H
