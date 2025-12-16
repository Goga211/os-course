#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "common.h"

#ifdef USE_VTPC
#include "vtpc.h"
#endif

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
} args_t;

static void usage(const char* prog) {
  fprintf(
      stderr,
      "Usage: %s "
      "--rw read|write "
      "--block_size N "
      "--block_count N "
      "--file PATH "
      "[--range A-B] "
      "[--direct on|off] "
      "[--type sequence|random] "
      "[--repeat K] "
      "[--seed S]\n",
      prog
  );
}

static int parse_range(char* s, off_t* lo, off_t* hi) {
  if (!s) {
    *lo = 0;
    *hi = 0;
    return 0;
  }
  char* dash = strchr(s, '-');
  if (!dash) return -1;
  *dash = '\0';

  errno = 0;
  long long a = strtoll(s, NULL, 10);
  if (errno) return -1;

  errno = 0;
  long long b = strtoll(dash + 1, NULL, 10);
  if (errno || a < 0 || b < 0) return -1;

  *lo = (off_t)a;
  *hi = (off_t)b;
  return 0;
}

static void parse_args(int argc, char** argv, args_t* A) {
  memset(A, 0, sizeof(*A));
  A->rw = MODE_READ;
  A->pick = PICK_SEQ;
  A->direct = 0;
  A->repeat = 1;
  A->seed = 123456u;

  static struct option longopts[] = {
      {         "rw", required_argument, 0, 1},
      { "block_size", required_argument, 0, 2},
      {"block_count", required_argument, 0, 3},
      {       "file", required_argument, 0, 4},
      {      "range", required_argument, 0, 5},
      {     "direct", required_argument, 0, 6},
      {       "type", required_argument, 0, 7},
      {     "repeat", required_argument, 0, 8},
      {       "seed", required_argument, 0, 9},
      {            0,                 0, 0, 0}
  };

  int c, idx = 0;
  while ((c = getopt_long(argc, argv, "", longopts, &idx)) != -1) {
    switch (c) {
      case 1:
        if (!strcmp(optarg, "read")) A->rw = MODE_READ;
        else if (!strcmp(optarg, "write")) A->rw = MODE_WRITE;
        else { usage(argv[0]); exit(2); }
        break;
      case 2: A->block_size = (size_t)strtoull(optarg, NULL, 10); break;
      case 3: A->block_count = strtol(optarg, NULL, 10); break;
      case 4: A->file_path = optarg; break;
      case 5:
        if (parse_range(optarg, &A->range_lo, &A->range_hi) != 0) {
          fprintf(stderr, "Bad --range, expected A-B\n");
          exit(2);
        }
        break;
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
      case 8:
        A->repeat = strtol(optarg, NULL, 10);
        if (A->repeat <= 0) A->repeat = 1;
        break;
      case 9: A->seed = (unsigned)strtoul(optarg, NULL, 10); break;
      default:
        usage(argv[0]);
        exit(2);
    }
  }

  if (!A->block_size || A->block_count <= 0 || !A->file_path) {
    usage(argv[0]);
    exit(2);
  }
}

static inline off_t align_down(off_t x, off_t a) {
  return (x / a) * a;
}

/* ---------- backend wrapper (OS or VTPC) ---------- */

#ifdef USE_VTPC

static int io_open(const char* path, int oflags, mode_t omode) {
  // vtpc сам делает O_DIRECT (в обход page cache ОС)
  return vtpc_open(path, oflags, (int)omode);
}

static int io_close(int fd) {
  return vtpc_close(fd);
}

static ssize_t io_pread(int fd, void* buf, size_t count, off_t off) {
  if (vtpc_lseek(fd, off, SEEK_SET) == (off_t)-1) return -1;
  return vtpc_read(fd, buf, count);
}

static ssize_t io_pwrite(int fd, const void* buf, size_t count, off_t off) {
  if (vtpc_lseek(fd, off, SEEK_SET) == (off_t)-1) return -1;
  return vtpc_write(fd, buf, count);
}

#else

static int io_open(const char* path, int oflags, mode_t omode) {
  return open(path, oflags, omode);
}

static int io_close(int fd) {
  return close(fd);
}

static ssize_t io_pread(int fd, void* buf, size_t count, off_t off) {
  return pread(fd, buf, count, off);
}

static ssize_t io_pwrite(int fd, const void* buf, size_t count, off_t off) {
  return pwrite(fd, buf, count, off);
}

#endif

/* ---------- main ---------- */

int main(int argc, char** argv) {
  args_t A;
  parse_args(argc, argv, &A);
  srandom(A.seed);

  const size_t ALIGN = 4096;

#ifndef USE_VTPC
  // В режиме VTPC эти ограничения не нужны:
  // user buffer может быть любым, O_DIRECT применяется внутри vtpc к своим aligned-страницам.
  if (A.direct) {
    if (A.block_size % ALIGN) {
      fprintf(stderr, "O_DIRECT: block_size must be multiple of %zu\n", ALIGN);
      return 2;
    }
    if (A.range_lo % ALIGN) {
      fprintf(stderr, "O_DIRECT: range start must be multiple of %zu\n", ALIGN);
      return 2;
    }
    if (A.range_hi && (A.range_hi % ALIGN)) {
      fprintf(stderr, "O_DIRECT: range end must be multiple of %zu\n", ALIGN);
      return 2;
    }
  }
#endif

  // Считаем размер файла через stat (и в vtpc-режиме тоже)
  struct stat st;
  memset(&st, 0, sizeof(st));
  if (stat(A.file_path, &st) != 0) {
    if (A.rw == MODE_READ) DIE("stat(%s): %s", A.file_path, strerror(errno));
    // для write: файл может не существовать — ок
    st.st_size = 0;
  }
  off_t file_size = st.st_size;

  off_t lo = A.range_lo;
  off_t hi = A.range_hi;

#ifndef USE_VTPC
  // только для baseline с реальным O_DIRECT
  if (A.direct) {
    lo = align_down(lo, (off_t)ALIGN);
    if (hi) hi = align_down(hi, (off_t)ALIGN);
  }
#endif

  if (A.rw == MODE_READ) {
    if (hi == 0) hi = file_size;
    if (hi <= lo) {
      fprintf(stderr, "read: empty range (lo=%lld, hi=%lld)\n",
              (long long)lo, (long long)hi);
      return 2;
    }
  } else {
    if (hi && hi <= lo) {
      fprintf(stderr, "write: empty range (lo=%lld, hi=%lld)\n",
              (long long)lo, (long long)hi);
      return 2;
    }

    // pre-extend file to hi if requested
    if (hi && hi > file_size) {
      if (truncate(A.file_path, hi) != 0) DIE("truncate: %s", strerror(errno));
      file_size = hi;
    }
  }

  int oflags = (A.rw == MODE_READ) ? O_RDONLY : (O_WRONLY | O_CREAT);
#ifndef USE_VTPC
  if (A.direct) oflags |= O_DIRECT;
#else
  // В vtpc-режиме --direct не влияет: vtpc всегда обходит page cache через O_DIRECT внутри
  (void)A.direct;
#endif

  mode_t omode = 0644;
  int fd = io_open(A.file_path, oflags, omode);
  if (fd < 0) DIE("open(%s): %s", A.file_path, strerror(errno));

  off_t usable_bytes = (hi > lo && hi > 0) ? (hi - lo) : 0;
  long slots_in_range = (hi > 0) ? (long)(usable_bytes / (off_t)A.block_size) : -1;

  // Буфер: в baseline при O_DIRECT нужен aligned, в vtpc можно обычный.
  void* buf = NULL;
#ifndef USE_VTPC
  int rc = posix_memalign(&buf, A.direct ? ALIGN : sizeof(void*), A.block_size);
  if (rc != 0 || !buf) DIE("posix_memalign");
#else
  buf = malloc(A.block_size);
  if (!buf) DIE("malloc");
#endif

  for (size_t i = 0; i < A.block_size; ++i)
    ((unsigned char*)buf)[i] = (unsigned char)(i * 131u + 7u);

  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);

  long long ops = 0;
  long long bytes = 0;
  off_t seq_index = 0;

  for (long r = 0; r < A.repeat; ++r) {
    for (long k = 0; k < A.block_count; ++k) {
      off_t idx;
      if (A.pick == PICK_SEQ) {
        idx = seq_index++;
      } else {
        if (slots_in_range > 0) idx = (off_t)(random() % (unsigned long)slots_in_range);
        else idx = (off_t)(random());
      }

      off_t base = lo + idx * (off_t)A.block_size;
      if (hi > 0 && base + (off_t)A.block_size > hi) {
        if (A.pick == PICK_SEQ) {
          seq_index = 0;
          base = lo;
        } else {
          continue;
        }
      }

      ssize_t n;
      if (A.rw == MODE_READ) n = io_pread(fd, buf, A.block_size, base);
      else n = io_pwrite(fd, buf, A.block_size, base);

      if (n < 0) {
        fprintf(stderr, "I/O error at off=%lld: %s\n",
                (long long)base, strerror(errno));
        free(buf);
        io_close(fd);
        return 1;
      }

      if (A.rw == MODE_READ && n == 0) break;

      if ((size_t)n < A.block_size) {
        fprintf(stderr, "short %s (%zd/%zu) at off=%lld\n",
                (A.rw == MODE_READ ? "read" : "write"),
                n, A.block_size, (long long)base);
        free(buf);
        io_close(fd);
        return 1;
      }

      ops++;
      bytes += n;
    }
  }

  clock_gettime(CLOCK_MONOTONIC, &t1);
  double ms = timespec_diff_ms(t0, t1);
  double mib = bytes / (1024.0 * 1024.0);
  double bw = (ms > 0) ? (mib / (ms / 1000.0)) : 0.0;
  double iops = (ms > 0) ? (ops / (ms / 1000.0)) : 0.0;

#ifdef USE_VTPC
  uint64_t hits = vtpc_cache_hits();
  uint64_t misses = vtpc_cache_misses();
  double hitrate = (hits + misses) ? (100.0 * (double)hits / (double)(hits + misses)) : 0.0;
#endif

  fprintf(
      stderr,
      "[io-load] rw=%s type=%s direct=%s bs=%zu bc=%ld file=%s range=%lld-%lld "
      "repeat=%ld seed=%u => bytes=%lld ops=%lld time=%.3f ms, BW=%.2f MiB/s, IOPS=%.0f"
#ifdef USE_VTPC
      " | cache: hits=%llu misses=%llu hitrate=%.2f%%"
#endif
      "\n",
      (A.rw == MODE_READ ? "read" : "write"),
      (A.pick == PICK_SEQ ? "sequence" : "random"),
      (A.direct ? "on" : "off"),
      A.block_size,
      A.block_count,
      A.file_path,
      (long long)A.range_lo,
      (long long)A.range_hi,
      A.repeat,
      A.seed,
      bytes,
      ops,
      ms,
      bw,
      iops
#ifdef USE_VTPC
      , (unsigned long long)hits
      , (unsigned long long)misses
      , hitrate
#endif
  );

  free(buf);
  io_close(fd);
  return 0;
}
