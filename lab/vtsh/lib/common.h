#pragma once

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>

#define DIE(...) do { \
  fprintf(stderr, __VA_ARGS__); \
  fprintf(stderr, "\n"); \
  exit(EXIT_FAILURE); \
} while(0)

static inline double timespec_diff_ms(struct timespec a, struct timespec b) {
  long sec  = b.tv_sec  - a.tv_sec;
  long nsec = b.tv_nsec - a.tv_nsec;
  return (double)sec * 1000.0 + (double)nsec / 1e6;
}
