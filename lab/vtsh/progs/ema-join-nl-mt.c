#include "common.h"
#include <pthread.h>

typedef struct {
    long id;
    char w[9];
} Row;

typedef struct {
    const Row *L;
    const Row *R;
    long NL;
    long NR;
    long start;           // [start, end)
    long end;

    FILE *out;
    pthread_mutex_t *mtx;

    // для замера времени потока
    double ms;
    int index;
} ThreadArg;

static Row* read_table(const char* path, long* outN) {
    FILE* f = fopen(path, "r");
    if (!f)
        DIE("open %s", path);

    long N = 0;
    if (fscanf(f, "%ld", &N) != 1) {
        fclose(f);
        DIE("bad header in %s", path);
    }

    Row* arr = calloc((size_t)N, sizeof(Row));
    if (!arr) {
        fclose(f);
        DIE("calloc");
    }

    for (long i = 0; i < N; ++i) {
        if (fscanf(f, "%ld %8s", &arr[i].id, arr[i].w) != 2) {
            fclose(f);
            free(arr);
            DIE("bad row in %s at %ld", path, i);
        }
        arr[i].w[8] = '\0';
    }

    fclose(f);
    *outN = N;
    return arr;
}

static void* thread_join(void *arg_) {
    ThreadArg *arg = (ThreadArg *)arg_;

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (long i = arg->start; i < arg->end; ++i) {
        long idL = arg->L[i].id;
        const char *wL = arg->L[i].w;

        for (long j = 0; j < arg->NR; ++j) {
            if (idL == arg->R[j].id) {
                const char *wR = arg->R[j].w;

                // Критическая секция: запись в общий файл
                pthread_mutex_lock(arg->mtx);
                fprintf(arg->out, "%ld %s %s\n", idL, wL, wR);
                pthread_mutex_unlock(arg->mtx);
            }
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    arg->ms = timespec_diff_ms(t0, t1);

    return NULL;
}

int main(int argc, char** argv) {
    const char *Lpath = NULL, *Rpath = NULL, *Opath = NULL;
    long repeat = 1;
    int threads = 1;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--left") && i + 1 < argc) {
            Lpath = argv[++i];
        } else if (!strcmp(argv[i], "--right") && i + 1 < argc) {
            Rpath = argv[++i];
        } else if (!strcmp(argv[i], "--out") && i + 1 < argc) {
            Opath = argv[++i];
        } else if (!strcmp(argv[i], "--repeat") && i + 1 < argc) {
            repeat = strtol(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "--threads") && i + 1 < argc) {
            threads = (int)strtol(argv[++i], NULL, 10);
        } else {
            fprintf(stderr,
                    "Usage: %s --left L --right R --out O "
                    "[--repeat K] [--threads T]\n",
                    argv[0]);
            return 2;
        }
    }

    if (!Lpath || !Rpath || !Opath) {
        fprintf(stderr, "--left/--right/--out required\n");
        return 2;
    }
    if (threads <= 0)
        threads = 1;

    long NL = 0, NR = 0;
    Row *L = read_table(Lpath, &NL);
    Row *R = read_table(Rpath, &NR);

    FILE *out = fopen(Opath, "w");
    if (!out)
        DIE("open %s", Opath);

    pthread_mutex_t mtx;
    pthread_mutex_init(&mtx, NULL);

    struct timespec T0, T1;
    clock_gettime(CLOCK_MONOTONIC, &T0);

    for (long k = 0; k < repeat; ++k) {
        // Для каждого повтора можно либо перетёреть файл, либо дописывать.
        // Здесь для простоты: первый repeat — w, остальные — a.
        if (k > 0) {
            fclose(out);
            out = fopen(Opath, "a");
            if (!out)
                DIE("reopen %s", Opath);
        }

        // Разбиваем диапазон [0, NL) на threads кусков
        pthread_t *tids = calloc((size_t)threads, sizeof(pthread_t));
        ThreadArg *args = calloc((size_t)threads, sizeof(ThreadArg));
        if (!tids || !args)
            DIE("calloc threads");

        long base = NL / threads;
        long rem  = NL % threads;
        long cur  = 0;

        for (int ti = 0; ti < threads; ++ti) {
            long len = base + (ti < rem ? 1 : 0);
            long start = cur;
            long end   = cur + len;
            cur = end;

            args[ti].L = L;
            args[ti].R = R;
            args[ti].NL = NL;
            args[ti].NR = NR;
            args[ti].start = start;
            args[ti].end = end;
            args[ti].out = out;
            args[ti].mtx = &mtx;
            args[ti].ms = 0.0;
            args[ti].index = ti;

            pthread_create(&tids[ti], NULL, thread_join, &args[ti]);
        }

        for (int ti = 0; ti < threads; ++ti) {
            pthread_join(tids[ti], NULL);
        }

        for (int ti = 0; ti < threads; ++ti) {
            fprintf(stderr,
                    "[ema-join-nl-mt] thread %d: %.3f ms (range [%ld, %ld))\n",
                    ti, args[ti].ms, args[ti].start, args[ti].end);
        }

        free(tids);
        free(args);
    }

    clock_gettime(CLOCK_MONOTONIC, &T1);
    double total_ms = timespec_diff_ms(T0, T1);

    fprintf(stderr,
            "[ema-join-nl-mt] L=%ld R=%ld repeat=%ld threads=%d "
            "total_time=%.3f ms -> %s\n",
            NL, NR, repeat, threads, total_ms, Opath);

    pthread_mutex_destroy(&mtx);
    fclose(out);
    free(L);
    free(R);
    return 0;
}
