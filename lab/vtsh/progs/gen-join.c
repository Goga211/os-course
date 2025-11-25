#include "common.h"

static unsigned long xorshift64(unsigned long* s) {
  unsigned long x = *s;
  x ^= x << 13;
  x ^= x >> 7;
  x ^= x << 17;
  return *s = x;
}

static void rand_word8(char out[9], unsigned long* st) {
  static const char ABC[] = "abcdefghijklmnopqrstuvwxyz";
  for (int i = 0; i < 8; ++i)
    out[i] = ABC[(int)(xorshift64(st) % 26u)];
  out[8] = '\0';
}

int main(int argc, char** argv) {
  const char* path = NULL;
  long rows = -1;
  unsigned long seed = 0x12345678u;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--out") && i + 1 < argc)
      path = argv[++i];
    else if (!strcmp(argv[i], "--rows") && i + 1 < argc)
      rows = strtol(argv[++i], NULL, 10);
    else if (!strcmp(argv[i], "--seed") && i + 1 < argc)
      seed = strtoul(argv[++i], NULL, 10);
    else {
      fprintf(
          stderr, "Usage: %s --out <file> --rows <N> [--seed S]\n", argv[0]
      );
      return 2;
    }
  }
  if (!path || rows < 0) {
    fprintf(stderr, "--out/--rows required\n");
    return 2;
  }

  FILE* f = fopen(path, "w");
  if (!f)
    DIE("open %s", path);

  fprintf(f, "%ld\n", rows);
  for (long i = 0; i < rows; ++i) {
    long id = (long)(xorshift64(&seed) % (rows * 2 + 10));
    char w[9];
    rand_word8(w, &seed);
    fprintf(f, "%ld %s\n", id, w);
  }
  fclose(f);
  return 0;
}
