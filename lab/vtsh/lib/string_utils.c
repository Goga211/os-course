#include "common.h"
#include "string_utils.h"

char* str_trim(char *s){
  if (!s) return s;
  char *p = s; while (*p==' '||*p=='\t'||*p=='\n') ++p;
  char *e = p + strlen(p);
  while (e>p && (e[-1]==' '||e[-1]=='\t'||e[-1]=='\n')) --e;
  *e = '\0';
  return p;
}

char** split_whitespace(const char *s, int *argc_out){
  *argc_out = 0; if (!s) return NULL;
  int n=0,in=0;
  for (const char *p=s; *p; ++p){
    if (*p==' '||*p=='\t'){ if(in){in=0; n++;} }
    else in=1;
  }
  if (in) n++;
  char **argv = calloc((size_t)n+1, sizeof(char*));
  if (!argv) DIE("calloc");

  int idx=0; const char *p=s;
  while (*p){
    while (*p==' '||*p=='\t') ++p;
    if (!*p) break;
    const char *q=p; while (*q && *q!=' ' && *q!='\t') ++q;
    size_t len=(size_t)(q-p);
    char *w = malloc(len+1); if(!w) DIE("malloc");
    memcpy(w,p,len); w[len]='\0';
    argv[idx++]=w; p=q;
  }
  argv[idx]=NULL; *argc_out=idx; return argv;
}

void free_argv(char **argv){
  if(!argv) return; for(int i=0; argv[i]; ++i) free(argv[i]); free(argv);
}
