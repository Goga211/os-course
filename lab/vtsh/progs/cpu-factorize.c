#include "common.h"

static unsigned long long strtoull_checked(const char* s){
  errno = 0;
  unsigned long long v = strtoull(s, NULL, 10);
  if (errno) DIE("bad --n");
  return v;
}

static void factor_once(unsigned long long n){
  unsigned long long orig = n;
  int first = 1;

  int cnt = 0; while ((n % 2ull) == 0ull){ n/=2ull; cnt++; }
  if (cnt){ printf("%s2^%d", first?"":" * ", cnt); first=0; }

  cnt = 0; while ((n % 3ull) == 0ull){ n/=3ull; cnt++; }
  if (cnt){ printf("%s3^%d", first?"":" * ", cnt); first=0; }

  for (unsigned long long f=5ull; f*f<=n; f+=6ull){
    int c1=0; while ((n%f)==0ull){ n/=f; c1++; }
    if (c1){ printf("%s%llu^%d", first?"":" * ", f, c1); first=0; }
    unsigned long long g = f+2ull;
    int c2=0; while ((n%g)==0ull){ n/=g; c2++; }
    if (c2){ printf("%s%llu^%d", first?"":" * ", g, c2); first=0; }
  }

  if (n>1ull){ printf("%s%llu", first?"":" * ", n); }
  printf("\n");
  (void)orig;
}

int main(int argc, char** argv){
  unsigned long long n = 0;
  long repeat = 1;

  for (int i=1;i<argc;i++){
    if (!strcmp(argv[i],"--n") && i+1<argc){ n = strtoull_checked(argv[++i]); }
    else if (!strcmp(argv[i],"--repeat") && i+1<argc){ repeat = strtol(argv[++i], NULL, 10); }
    else { fprintf(stderr,"Usage: %s --n <number> [--repeat K]\n", argv[0]); return 2; }
  }
  if (!n){ fprintf(stderr,"--n required\n"); return 2; }

  struct timespec t0,t1; clock_gettime(CLOCK_MONOTONIC,&t0);
  for (long k=0;k<repeat;++k) factor_once(n);
  clock_gettime(CLOCK_MONOTONIC,&t1);

  fprintf(stderr,"[cpu-factorize] n=%llu repeat=%ld time=%.3f ms\n",
          n, repeat, timespec_diff_ms(t0,t1));
  return 0;
}
