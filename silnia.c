//
// Created by Hubert on 04.01.2020.
//

#include "future.h"
#include "threadpool.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#define THREADS 3

typedef struct input input_t;

struct input {
    u_int64_t currProd;
    u_int64_t nextNum;
};

void* fun(void* arg, size_t argsz __attribute__((unused)), size_t* argP __attribute__((unused))) {
    input_t* in = (input_t*)arg;

    input_t* _in = (input_t*)malloc(sizeof(input_t));
    _in->currProd = in->currProd * in->nextNum;
    _in->nextNum = in->nextNum + 1;

    free(in);

    return _in;
}

int main () {
    size_t k;
    scanf("%zu", &k);

    thread_pool_t pool;
    if (thread_pool_init(&pool, THREADS) != 0)
        exit(1);

    input_t* in = malloc(sizeof(input_t));
    future_t* fut = (future_t*)malloc(sizeof(future_t) * k);
    if (!in || !fut) {
        free(in);
        free(fut);
        thread_pool_destroy(&pool);

        exit(1);
    }

    in->currProd = 1;
    in->nextNum = 1;

    callable_t call;
    call.arg = in;
    call.argsz = sizeof(input_t);
    call.function = fun;

    if (async(&pool, &fut[0], call) != 0) {
        free(in);
        free(fut);
        thread_pool_destroy(&pool);

        exit(1);
    }

    for (size_t i = 0; i < k - 1; i++) {
        if (map(&pool, &fut[i + 1], &fut[i], fun) != 0) {
            free(fut);
            thread_pool_destroy(&pool);

            exit(1);
        }
    }

    input_t* result = (input_t*)await(&fut[k - 1]);
    free(fut);


    if (result) {
        printf("%" PRIu64 "\n", result->currProd);

        free(result);
    }
    else {
        exit(1);
    }

    thread_pool_destroy(&pool);

    return 0;
}