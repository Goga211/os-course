#define _GNU_SOURCE
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif

#include <fcntl.h>
#include <getopt.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "common.h"

typedef enum { MODE_READ, MODE_WRITE } rw_mode_t;
typedef enum { PICK_SEQ, PICK_RND } pick_t;

typedef struct {
    rw_mode_t rw;
    size_t block_size;
    long block_count;
    const char* file_path;
    off_t range_lo;
    off_t range_hi;
    int direct;
    pick_t pick;
    long repeat;
    unsigned seed;
    int threads;
} args_t;

/* ======================================================================= */
/*                               PARSE ARGS                                */
/* ======================================================================= */

static void usage(const char* prog) {
    fprintf(stderr,
        "Usage: %s "
        "--rw read|write "
        "--block_size N "
        "--block_count N "
        "--file PATH "
        "[--range A-B] "
        "[--direct on|off] "
        "[--type sequence|random] "
        "[--repeat K] "
        "[--seed S] "
        "[--threads N]\n",
        prog);
}

static int parse_range(const char* s, off_t* lo, off_t* hi) {
    if (!s) {
        *lo = 0; *hi = 0;
        return 0;
    }
    char* tmp = strdup(s);
    char* dash = strchr(tmp, '-');
    *dash = '\0';

    *lo = atoll(tmp);
    *hi = atoll(dash + 1);

    free(tmp);
    return 0;
}

static void parse_args(int argc, char** argv, args_t* A) {
    memset(A, 0, sizeof(*A));
    A->rw = MODE_READ;
    A->pick = PICK_SEQ;
    A->threads = 1;
    A->repeat = 1;
    A->seed = 1234;

    static struct option longopts[] = {
        {"rw", required_argument, 0, 1},
        {"block_size", required_argument, 0, 2},
        {"block_count", required_argument, 0, 3},
        {"file", required_argument, 0, 4},
        {"range", required_argument, 0, 5},
        {"direct", required_argument, 0, 6},
        {"type", required_argument, 0, 7},
        {"repeat", required_argument, 0, 8},
        {"seed", required_argument, 0, 9},
        {"threads", required_argument, 0, 10},
        {0,0,0,0}
    };

    int c, idx = 0;
    while ((c = getopt_long(argc, argv, "", longopts, &idx)) != -1) {
        switch (c) {
            case 1:
                if (!strcmp(optarg, "read")) A->rw = MODE_READ;
                else if (!strcmp(optarg, "write")) A->rw = MODE_WRITE;
                else { usage(argv[0]); exit(2); }
                break;
            case 2: A->block_size = atoll(optarg); break;
            case 3: A->block_count = atoll(optarg); break;
            case 4: A->file_path = optarg; break;
            case 5: parse_range(optarg, &A->range_lo, &A->range_hi); break;
            case 6:
                if (!strcmp(optarg, "on")) A->direct = 1;
                else if (!strcmp(optarg, "off")) A->direct = 0;
                else { usage(argv[0]); exit(2); }
                break;
            case 7:
                if (!strcmp(optarg, "sequence")) A->pick = PICK_SEQ;
                else if (!strcmp(optarg, "random")) A->pick = PICK_RND;
                else { usage(argv[0]); exit(2); }
                break;
            case 8: A->repeat = atoll(optarg); break;
            case 9: A->seed = atoll(optarg); break;
            case 10: A->threads = atoi(optarg); break;
            default: usage(argv[0]); exit(2);
        }
    }

    if (!A->block_size || !A->block_count || !A->file_path) {
        usage(argv[0]);
        exit(2);
    }
}

/* ======================================================================= */
/*                               WORKER THREADS                             */
/* ======================================================================= */

typedef struct {
    const args_t* A;
    int fd;
    off_t lo, hi;
    long slots;

    long tid;
    long long start_blk, end_blk;

    long long ops;
    long long bytes;
    double elapsed_ms;
} worker_t;

/* simple PRNG */
static inline unsigned long long rng_next(unsigned long long* x) {
    *x ^= *x >> 12;
    *x ^= *x << 25;
    *x ^= *x >> 27;
    return *x * 2685821657736338717ULL;
}

static void* worker_main(void* arg) {
    worker_t* W = arg;
    const args_t* A = W->A;

    void* buf = NULL;
    posix_memalign(&buf, (A->direct ? 4096 : sizeof(void*)), A->block_size);

    unsigned long long rng = A->seed + W->tid * 1337;

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (long long i = W->start_blk; i < W->end_blk; ++i) {

        off_t slot;
        if (A->pick == PICK_SEQ)
            slot = (i % W->slots);
        else
            slot = rng_next(&rng) % W->slots;

        off_t off = W->lo + slot * A->block_size;

        ssize_t n = (A->rw == MODE_READ)
            ? pread(W->fd, buf, A->block_size, off)
            : pwrite(W->fd, buf, A->block_size, off);

        W->ops++;
        W->bytes += n;
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    W->elapsed_ms = timespec_diff_ms(t0, t1);

    free(buf);
    return NULL;
}

/* ======================================================================= */
/*                                MAIN                                      */
/* ======================================================================= */

int main(int argc, char** argv)
{
    args_t A;
    parse_args(argc, argv, &A);

    int fd = open(A.file_path,
                  (A.rw == MODE_READ ? O_RDONLY : (O_WRONLY | O_CREAT)),
                  0644);

    struct stat st;
    fstat(fd, &st);
    off_t lo = A.range_lo;
    off_t hi = (A.range_hi ? A.range_hi : st.st_size);

    long slots = (hi - lo) / A.block_size;
    long long total_blocks = (long long)A.block_count * A.repeat;

    if (A.threads > total_blocks) A.threads = total_blocks;

    long long per = total_blocks / A.threads;
    long long rem = total_blocks % A.threads;

    pthread_t th[A.threads];
    worker_t ctx[A.threads];

    long long cur = 0;
    printf("=== io-load-mt: threads=%d bs=%zu bc=%ld range=[%lld,%lld] ===\n",
            A.threads, A.block_size, A.block_count,
            (long long)lo, (long long)hi);

    struct timespec T0, T1;
    clock_gettime(CLOCK_MONOTONIC, &T0);

    for (int i = 0; i < A.threads; i++) {
        long long add = per + (i < rem ? 1 : 0);

        ctx[i].A = &A;
        ctx[i].fd = fd;
        ctx[i].lo = lo;
        ctx[i].hi = hi;
        ctx[i].slots = slots;
        ctx[i].tid = i;
        ctx[i].start_blk = cur;
        ctx[i].end_blk = cur + add;
        ctx[i].ops = 0;
        ctx[i].bytes = 0;

        cur += add;

        pthread_create(&th[i], NULL, worker_main, &ctx[i]);
    }

    long long total_ops = 0, total_bytes = 0;

    for (int i = 0; i < A.threads; i++) {
        pthread_join(th[i], NULL);
        printf("[thread %2d] blocks=[%lld..%lld) time=%.3f ms ops=%lld bytes=%lld\n",
               i,
               ctx[i].start_blk,
               ctx[i].end_blk,
               ctx[i].elapsed_ms,
               ctx[i].ops,
               ctx[i].bytes);

        total_ops += ctx[i].ops;
        total_bytes += ctx[i].bytes;
    }

    clock_gettime(CLOCK_MONOTONIC, &T1);
    double total_ms = timespec_diff_ms(T0, T1);

    printf("[io-load-mt] TOTAL: time=%.3f ms, ops=%lld, bytes=%lld\n",
           total_ms, total_ops, total_bytes);

    close(fd);
    return 0;
}
