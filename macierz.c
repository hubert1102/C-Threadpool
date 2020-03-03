//
// Created by Hubert on 02.01.2020.
//

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include "threadpool.h"

#define THREADS 4

typedef struct input {
    int *matrix;
    int *times;
    int *solution;
    int row;
    int columns;
} input;

void *calcSum(input* arg, size_t __attribute__((unused))argsz) {
    input* in = (input*)arg;
    int columns = in->columns;
    int* times = in->times;
    int* matrix = in->matrix;
    int row = in->row;

    int sum = 0;

    for (int i = 0; i < columns; i++) {
        usleep(times[row * columns + i]);
        sum += matrix[row * columns + i];
    }

    in->solution[row] = sum;
    return NULL;
}

int main () {
    size_t k, n;

    scanf("%zu", &k);
    scanf("%zu", &n);

    int* matrix = (int*)malloc(sizeof(int) * n * k);
    int* times = (int*)malloc(sizeof(int) * n * k);
    int* solution = (int*)malloc(sizeof(int) * k);
    input** inputs = (input**)malloc(sizeof(input*) * k);

    if (!matrix || !times) {
        free(matrix);
        free(times);
        free(solution);
        free(inputs);

        exit(1);
    }
    thread_pool_t pool;
    if (thread_pool_init(&pool, THREADS) != 0) {
        free(matrix);
        free(times);
        free(solution);
        free(inputs);

        exit(1);
    }

    for (size_t i = 0; i < k * n; i++) {
        int v;
        size_t t;
        scanf("%d", &v);
        scanf("%zu", &t);

        matrix[i] = v;
        times[i] = t;
    }


    for (size_t i = 0; i < k; i++) {
        inputs[i] = (input*)malloc(sizeof(input));

        if (!inputs[i]) {
            thread_pool_destroy(&pool);
            free(matrix);
            free(times);
            free(solution);

            for (size_t j = 0; j < i; j++)
                free(inputs[j]);
            free(inputs);

            exit(1);
        }
        inputs[i]->matrix = matrix;
        inputs[i]->times = times;
        inputs[i]->columns = n;
        inputs[i]->row = i;
        inputs[i]->solution = solution;

        runnable_t run;
        run.argsz = 1;
        run.arg = inputs[i];
        run.function = (void *)calcSum;

        if (defer(&pool, run) != 0) {
            thread_pool_destroy(&pool);
            free(matrix);
            free(times);
            free(solution);

            for (size_t j = 0; j < i; j++)
                free(inputs[j]);
            free(inputs);

            exit(1);
        }
    }

    thread_pool_destroy(&pool);
    free(matrix);
    free(times);

    for (size_t i = 0; i < k; i++) {
        printf("%d\n", solution[i]);
        free(inputs[i]);
    }

    free(inputs);
    free(solution);

    return 0;
}
