#include "common.h"
#include "vtsh.h"
#include "string_utils.h"
#include "parse.h"

#include <unistd.h>
#include <sys/wait.h> 

extern int vtsh_run_argv(char **argv);

static int is_blank(const char *s){
  if (!s) return 1;
  for (; *s; ++s) if(*s!=' ' && *s!='\t' && *s!='\n') return 0;
  return 1;
}

static int builtin_cd(char **argv){
  const char *path = argv[1] ? argv[1] : getenv("HOME");
  if (!path){ fprintf(stderr,"cd: HOME is not set\n"); return 1; }
  if (chdir(path) != 0){ perror("cd"); return 1; }
  return 0;
}

static int builtin_exit(char **argv){
  (void)argv;
  exit(0);
  return 0;
}

static int builtin_shell(char **argv){
  (void)argv;
  pid_t pid = fork();
  if (pid < 0) { perror("fork"); return 1; }

  if (pid == 0) {
    char self[4096];
    ssize_t n = readlink("/proc/self/exe", self, sizeof(self)-1);
    if (n < 0) _exit(127);
    self[n] = '\0';
    char *const args[] = { self, NULL };
    execv(self, args);
    _exit(127);
  }

  int st = 0;
  if (waitpid(pid, &st, 0) < 0) { perror("waitpid"); return 1; }
  if (WIFEXITED(st))   return WEXITSTATUS(st);
  if (WIFSIGNALED(st)) return 128 + WTERMSIG(st);
  return 1;
}

static int is_builtin(const char *cmd){
  return  !strcmp(cmd,"cd")
       || !strcmp(cmd,"exit")
       || !strcmp(cmd,"shell")
       || !strcmp(cmd,"./shell");
}

static int run_builtin(char **argv){
  if (!strcmp(argv[0],"cd"))        return builtin_cd(argv);
  if (!strcmp(argv[0],"exit"))      return builtin_exit(argv);
  if (!strcmp(argv[0],"shell")
   || !strcmp(argv[0],"./shell"))   return builtin_shell(argv);
  return 1;
}

int vtsh_run_line_or(const char* line){
  size_t nseg = 0;
  int last_rc = 1;

  char **segs = split_by_or(line, &nseg);
  for (size_t i = 0; i < nseg; ++i){
    int argc = 0;
    char **argv = split_whitespace(segs[i], &argc);
    if (!argv || !argv[0]) { free_argv(argv); continue; }

    int rc = is_builtin(argv[0]) ? run_builtin(argv) : vtsh_run_argv(argv);
    free_argv(argv);
    last_rc = rc;

    if (rc == 0) break;
  }

  for (size_t i=0;i<nseg;++i) free(segs[i]);
  free(segs);
  return last_rc;
}

int vtsh_repl(void){
  const int interactive = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);

  char *line = NULL;
  size_t cap = 0;

  while (1){
    if (interactive) { fputs("mysh$ ", stderr); fflush(stderr); }

    ssize_t n = getline(&line, &cap, stdin);
    if (n < 0) break;
    if (n > 0 && line[n-1] == '\n') line[n-1] = '\0';
    if (is_blank(line)) continue;

    (void)vtsh_run_line_or(line);
  }

  free(line);
  return 0;
}
