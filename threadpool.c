#include "threadpool.h"
#include "threadpoolPrivate.h"
#include "err.h"
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

enum error {
    malloc_failed = -1, pool_destroyed = -2
};

// List of jobs to be processed
typedef struct job_list {
    runnable_t fun;

    struct job_list* next;
    struct job_list* end;
} job_list;


/// SIGINT handling ///
typedef struct poolList poolList;

typedef struct poolList {
    poolList* next;
    poolList* end;
    thread_pool_t* pool;
} poolList_t;

poolList_t* newPoolList() {
    poolList_t* list = (poolList*)malloc(sizeof(poolList));

    if (!list)
        return NULL;

    list->next = NULL;
    list->pool = NULL;
    list->end = NULL;

    return list;
}
poolList_t* P_list = NULL;

void popPoolList(thread_pool_t* pool) {
    poolList_t* p = P_list;

    while(p->next && p->next->pool != pool)
        p = p->next;

    poolList_t* _p = p->next;
    p->next = p->next->next;
    if (!p->next)
        P_list->end = p;

    free(_p);
}

int addPool(thread_pool_t* pool) {
    poolList* _list = newPoolList();
    if (!_list)
        return malloc_failed;

    _list->pool = pool;
    if (!P_list->end) {
        P_list->end = _list;
        P_list->next = _list;

        return 0;
    }

    P_list->end->next = _list;
    P_list->end = _list;

    return 0;
}

bool poolListEmpty() {
    if (!P_list->next)
        return true;

    return false;
}

void destroyPools() {
    while (P_list) {
        poolList* _list = P_list->next;
        thread_pool_destroy(P_list->pool);
        free(P_list);
        P_list = _list;
    }

    exit(1);
}

void enableSIGINTHandling() {
    int err;
    struct sigaction action;
    sigset_t block_mask;

    sigemptyset (&block_mask);
    sigaddset(&block_mask, SIGINT);

    action.sa_handler = destroyPools;
    action.sa_mask = block_mask;
    action.sa_flags = 0;

    if ((err = sigaction (SIGINT, &action, 0)) != 0)
        syserr(err, "sigaction");
}

//// Job List ////
job_list* newJobList() {
    job_list* list = (job_list *) malloc(sizeof(job_list));

    if (!list)
        return NULL;

    list->end = list;
    list->next = NULL;

    return list;
}

int add(job_list* list, runnable_t fun) {
    job_list* _list = (job_list*)malloc(sizeof(job_list));

    if (!_list)
        return malloc_failed;

    _list->next = NULL;
    _list->fun = fun;

    list->end->next = _list;
    list->end = _list;

    return 0;
}

runnable_t pop(job_list* list) {
    runnable_t fun = list->next->fun;

    job_list* _list = list->next;
    list->next = _list->next;
    free(_list);

    if (!list->next)
        list->end = list;

    return fun;
}

bool empty_queue(job_list* list) {
    if (list && list->next)
        return false;

    return true;
}

void free_list(job_list* list) {
    while (!empty_queue(list))
        pop(list);

    free(list);
}

//// job_distributor ////
job_distributor* newJobDistributor() {
    int err;

    job_distributor* distributor = (job_distributor*)malloc(sizeof(job_distributor));
    if (!distributor)
        return NULL;

    distributor->shutdown = false;
    distributor->threads_waiting = 0;
    distributor->threads_working = 0;
    distributor->jobs = newJobList();
    if (!distributor->jobs) {
        free(distributor);

        return NULL;
    }

    if (pthread_cond_init(&distributor->available_jobs, 0) != 0) {
        free(distributor->jobs);
        free(distributor);

        return NULL;
    }

    if (pthread_cond_init(&distributor->shutdown_wait, 0)) {
        free(distributor->jobs);
        if ((err = pthread_cond_destroy (&distributor->available_jobs)) != 0)
            syserr (err, "cond destroy failed");

        free(distributor);
        return NULL;
    }

    if (pthread_mutex_init(&distributor->lock, 0)) {
        free(distributor->jobs);
        if ((err = pthread_cond_destroy (&distributor->available_jobs)) != 0)
            syserr (err, "cond destroy failed");
        if ((err = pthread_cond_destroy (&distributor->shutdown_wait)) != 0)
            syserr (err, "cond destroy 2 failed");

        free(distributor);
        return NULL;
    }

    return distributor;
}

int add_job(job_distributor* q, runnable_t runnable) {
    int err = 0;
    int ret = 0;

    if ((err = pthread_mutex_lock(&q->lock)) != 0)
        syserr(err, "add_job lock failed");

    if (q->shutdown) {
        if ((err = pthread_mutex_unlock(&q->lock)) != 0)
            syserr (err, "unlock failed");

        return pool_destroyed;
    }

    ret = add(q->jobs, runnable);


    if (q->threads_waiting)
        if ((err = pthread_cond_signal(&q->available_jobs)) != 0)
            syserr (err, "cond signal failed");

    if ((err = pthread_mutex_unlock(&q->lock)) != 0)
        syserr (err, "unlock failed");

    return ret;
}

bool give_job(job_distributor* q, runnable_t* fun) {
    int err;

    if ((err = pthread_mutex_lock(&q->lock)) != 0)
        syserr(err, "give_job lock failed");

    q->threads_waiting++;

    while (empty_queue(q->jobs)) {
        if (q->shutdown) {
            if ((err = pthread_mutex_unlock(&q->lock)) != 0)
                syserr (err, "unlock failed");

            return false;
        }

        if ((err = pthread_cond_wait(&q->available_jobs, &q->lock)) != 0)
            syserr (err, "cond wait failed");
    }

    *fun = pop(q->jobs);

    q->threads_waiting--;
    q->threads_working++;

    if ((err = pthread_mutex_unlock(&q->lock)) != 0)
        syserr (err, "unlock failed");

    return true;
}


void *worker(void *monitor) {

    job_distributor* distributor = (job_distributor*) monitor;
    int err;

    while (true) {
        runnable_t fun; //= (runnable_t*)malloc(sizeof(runnable_t));
        bool sd = give_job(distributor, &fun);

        if (distributor->shutdown && !sd)
            return NULL;

        fun.function(fun.arg, fun.argsz);

        //free(fun);
        if ((err = pthread_mutex_lock(&distributor->lock)) != 0)
            syserr(err, "lock failed");

        distributor->threads_working--;

        if (distributor->shutdown && !distributor->threads_working
            && empty_queue(distributor->jobs)) {
            if ((err = pthread_cond_signal(&distributor->shutdown_wait)) != 0)
                syserr(err, "signal shutdown_wait failed");

            if ((err = pthread_mutex_unlock(&distributor->lock)) != 0)
                syserr(err, "unlock failed");

            return NULL;
        }

        if ((err = pthread_mutex_unlock(&distributor->lock)) != 0)
            syserr(err, "unlock failed");
    }
}

void distributor_destroy(job_distributor* distributor) {
    free_list(distributor->jobs);
    distributor->jobs = NULL;

    int err;

    if ((err = pthread_cond_destroy (&distributor->available_jobs)) != 0)
        syserr (err, "cond destroy 1 failed");
    if ((err = pthread_mutex_destroy (&distributor->lock)) != 0)
        syserr (err, "mutex destroy failed");
    if ((err = pthread_cond_destroy (&distributor->shutdown_wait)) != 0)
        syserr (err, "cond destroy 2 failed");
}

//// thread_pool ////
int thread_pool_init(thread_pool_t *pool, size_t num_threads) {
    int err;

    pool->pool_size = num_threads;
    pool->distributor = newJobDistributor();

    if (!pool->distributor)
        return malloc_failed;

    pool->threads = (pthread_t*)malloc(sizeof(pthread_t) * num_threads);
    if (!pool->threads) {
        distributor_destroy(pool->distributor);

        return malloc_failed;
    }

    for (size_t i = 0; i < num_threads; i++) {
        if ((err = pthread_create(&pool->threads[i], NULL, worker, pool->distributor)) != 0) {
            distributor_destroy(pool->distributor);
            free(pool->distributor);
            free(pool->threads);

            return err;
        }
    }

    if (!P_list) {
        P_list = newPoolList();
        if (!P_list) {
            thread_pool_destroy(pool);
            return malloc_failed;
        }


        if (addPool(pool) != 0) {
            thread_pool_destroy(pool);
            return malloc_failed;
        }
    }

    enableSIGINTHandling();
    return 0;
}

void thread_pool_destroy(struct thread_pool *pool) {
    int err;

    if ((err = pthread_mutex_lock(&pool->distributor->lock)) != 0)
        syserr(err, "destroy lock failed");

    pool->distributor->shutdown = true;


    popPoolList(pool);

    if (poolListEmpty()) {
        free(P_list);
        P_list = NULL;
    }

    while (!empty_queue(pool->distributor->jobs)) {
        if ((err = pthread_cond_wait(&pool->distributor->shutdown_wait, &pool->distributor->lock)) != 0)
            syserr(err, "cond wait failed");
    }

    if ((err = pthread_mutex_unlock(&pool->distributor->lock)) != 0)
        syserr(err, "unlock failed");

    // Every worker thread waits for new jobs, after being awakend they end.
    pthread_cond_broadcast(&pool->distributor->available_jobs);

    for (size_t i = 0; i < pool->pool_size; i++) {
        if ((err = pthread_join(pool->threads[i], NULL)) != 0)
            syserr(err, "join failed");
    }

    distributor_destroy(pool->distributor);
    free(pool->distributor);
    free(pool->threads);

    pool->distributor = NULL;

}

int defer(struct thread_pool *pool, runnable_t runnable) {
    return add_job(pool->distributor, runnable);
}






