#include "common.h"
#include "string_utils.h"
#include "parse.h"

static char* xstrdup(const char *s){
  size_t n = strlen(s)+1;
  char *p = malloc(n);
  if(!p) DIE("malloc");
  memcpy(p,s,n);
  return p;
}

char** split_by_or(const char *line, size_t *count){
  *count = 0; if (!line) return NULL;

  size_t parts = 1;
  for (const char *p=line; *p; ++p)
    if (p[0]=='|' && p[1]=='|'){ parts++; p++; }

  char **arr = calloc(parts, sizeof(char*));
  if (!arr) DIE("calloc");

  char *buf = xstrdup(line);
  size_t idx=0; char *start=buf;
  for (size_t i=0; buf[i]; ++i){
    if (buf[i]=='|' && buf[i+1]=='|'){
      buf[i]='\0';
      arr[idx++] = xstrdup(str_trim(start));
      start = buf + i + 2;
    }
  }
  arr[idx++] = xstrdup(str_trim(start));

  *count = idx;
  free(buf);
  return arr;
}
