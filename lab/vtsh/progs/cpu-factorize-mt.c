#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdatomic.h>
#include <time.h>
#include <errno.h>

// ------------------------
// Тайминг
// ------------------------
static double timespec_diff_ms(struct timespec a, struct timespec b) {
    return (b.tv_sec - a.tv_sec) * 1000.0 +
           (b.tv_nsec - a.tv_nsec) / 1e6;
}

// ------------------------
// ТОЧНО ТАКОЙ ЖЕ АЛГОРИТМ,
// как в процессной версии
// ------------------------
static void factor_once(unsigned long long n) {
    int first = 1;
    int cnt = 0;

    // Проверка деления на 2
    cnt = 0;
    while ((n % 2ull) == 0ull) {
        n /= 2ull;
        cnt++;
    }
    if (cnt) {
        // printf("%s2^%d", first ? "" : " * ", cnt);
        first = 0;
    }

    // Проверка деления на 3
    cnt = 0;
    while ((n % 3ull) == 0ull) {
        n /= 3ull;
        cnt++;
    }
    if (cnt) {
        // printf("%s3^%d", first ? "" : " * ", cnt);
        first = 0;
    }

    // Главный цикл 6k ± 1
    for (unsigned long long f = 5ull; f * f <= n; f += 6ull) {
        int c1 = 0;
        while ((n % f) == 0ull) {
            n /= f;
            c1++;
        }
        if (c1) {
            // printf("%s%llu^%d", first ? "" : " * ", f, c1);
            first = 0;
        }

        unsigned long long g = f + 2ull;
        int c2 = 0;
        while ((n % g) == 0ull) {
            n /= g;
            c2++;
        }
        if (c2) {
            // printf("%s%llu^%d", first ? "" : " * ", g, c2);
            first = 0;
        }
    }

    if (n > 1ull) {
        // printf("%s%llu", first ? "" : " * ", n);
    }
    // printf("\n");
}

// ------------------------
// Аргументы потока
// ------------------------
typedef struct {
    long thread_id;
    unsigned long long n;
    long repeat_total;      // общая работа
    atomic_long* next_job;  // atomic распределение задач
    double out_ms;
} thread_arg_t;

// ------------------------
// Функция потока
// ------------------------
static void* thread_func(void* p) {
    thread_arg_t* A = (thread_arg_t*)p;

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    while (1) {
        long job = atomic_fetch_add(A->next_job, 1);

        if (job >= A->repeat_total)
            break;

        factor_once(A->n);
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    A->out_ms = timespec_diff_ms(t0, t1);

    return NULL;
}

// ------------------------
// MAIN
// ------------------------
int main(int argc, char** argv) {
    unsigned long long n = 0;
    long repeat = 1;
    long threads = 1;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--n") && i + 1 < argc) {
            n = strtoull(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "--repeat") && i + 1 < argc) {
            repeat = strtol(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "--threads") && i + 1 < argc) {
            threads = strtol(argv[++i], NULL, 10);
        } else {
            fprintf(stderr,
                "Usage: %s --n <number> --repeat <K> --threads <T>\n",
                argv[0]);
            return 2;
        }
    }

    if (n == 0 || repeat <= 0 || threads <= 0) {
        fprintf(stderr, "Bad parameters\n");
        return 2;
    }

    pthread_t* th = calloc(threads, sizeof(pthread_t));
    thread_arg_t* args = calloc(threads, sizeof(thread_arg_t));

    atomic_long next_job = 0;

    struct timespec T0, T1;
    clock_gettime(CLOCK_MONOTONIC, &T0);

    // Запуск потоков
    for (long t = 0; t < threads; t++) {
        args[t].thread_id = t;
        args[t].n = n;
        args[t].repeat_total = repeat;
        args[t].next_job = &next_job;
        args[t].out_ms = 0.0;

        if (pthread_create(&th[t], NULL, thread_func, &args[t]) != 0) {
            perror("pthread_create");
            return 1;
        }
    }

    // Join
    for (long t = 0; t < threads; t++)
        pthread_join(th[t], NULL);

    clock_gettime(CLOCK_MONOTONIC, &T1);
    double total_ms = timespec_diff_ms(T0, T1);

    // Итог
    fprintf(stderr,
        "[cpu-factorize-mt] threads=%ld n=%llu repeat=%ld total=%.3f ms\n",
        threads, n, repeat, total_ms);

    free(th);
    free(args);
    return 0;
}
