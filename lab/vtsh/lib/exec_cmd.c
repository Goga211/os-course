#include "common.h"
#include "string_utils.h"

static int run_one_command(char **argv){
  if (!argv || !argv[0]) return 0;

  struct timespec t0,t1; clock_gettime(CLOCK_MONOTONIC,&t0);
  pid_t pid = fork();
  if (pid < 0){ perror("fork"); return 127; }
  if (pid == 0){
    execvp(argv[0], argv);
    perror("execvp");
    _exit(127);
  }

  int st=0;
  if (waitpid(pid, &st, 0) < 0){ perror("waitpid"); st = 127<<8; }
  clock_gettime(CLOCK_MONOTONIC,&t1);

  int rc=0;
  if (WIFEXITED(st)) rc = WEXITSTATUS(st);
  else if (WIFSIGNALED(st)) rc = 128 + WTERMSIG(st);
  else rc = 1;

  if (rc == 127) {
    fputs("Command not found\n", stdout);
    fflush(stdout);
  }

  fprintf(stderr, "[pid=%d] exit=%d time=%.3f ms\n",
          (int)pid, rc, timespec_diff_ms(t0,t1));
  return rc;
}

// Экспорт для vtsh.c
int vtsh_run_argv(char **argv){ return run_one_command(argv); }
