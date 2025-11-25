#include "common.h"

typedef struct {
  long id;
  char w[9];
} Row;

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

static void write_join(
    const char* path, const Row* L, long NL, const Row* R, long NR
) {
  FILE* o = fopen(path, "w");
  if (!o)
    DIE("open %s", path);

  for (long i = 0; i < NL; ++i) {
    for (long j = 0; j < NR; ++j) {
      if (L[i].id == R[j].id) {
        fprintf(o, "%ld %s %s\n", L[i].id, L[i].w, R[j].w);
      }
    }
  }
  fclose(o);
}

int main(int argc, char** argv) {
  const char *Lpath = NULL, *Rpath = NULL, *Opath = NULL;
  long repeat = 1;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--left") && i + 1 < argc)
      Lpath = argv[++i];
    else if (!strcmp(argv[i], "--right") && i + 1 < argc)
      Rpath = argv[++i];
    else if (!strcmp(argv[i], "--out") && i + 1 < argc)
      Opath = argv[++i];
    else if (!strcmp(argv[i], "--repeat") && i + 1 < argc)
      repeat = strtol(argv[++i], NULL, 10);
    else {
      fprintf(
          stderr, "Usage: %s --left L --right R --out O [--repeat K]\n", argv[0]
      );
      return 2;
    }
  }
  if (!Lpath || !Rpath || !Opath) {
    fprintf(stderr, "--left/--right/--out required\n");
    return 2;
  }

  long NL = 0, NR = 0;
  Row* L = read_table(Lpath, &NL);
  Row* R = read_table(Rpath, &NR);

  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  for (long k = 0; k < repeat; ++k) {
    write_join(Opath, L, NL, R, NR);
  }
  clock_gettime(CLOCK_MONOTONIC, &t1);

  fprintf(
      stderr,
      "[ema-join-nl] L=%ld R=%ld repeat=%ld time=%.3f ms -> %s\n",
      NL,
      NR,
      repeat,
      timespec_diff_ms(t0, t1),
      Opath
  );

  free(L);
  free(R);
  return 0;
}
