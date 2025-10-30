#include "common.h"

int main(int argc, char **argv){
  if (argc < 2){
    fprintf(stderr, "Usage: %s <prog> [args...]\n", argv[0]);
    return 2;
  }

  pid_t pid = fork();
  if (pid < 0) {
    DIE("fork: %s", strerror(errno));
  }

  if (pid == 0) {
    execvp(argv[1], &argv[1]);
    perror("execvp");
    _exit(127);
  }

  int st = 0;
  if (waitpid(pid, &st, 0) < 0) {
    DIE("waitpid: %s", strerror(errno));
  }

  if (WIFEXITED(st)) {
    printf("child pid=%d exit=%d\n", (int)pid, WEXITSTATUS(st));
    return WEXITSTATUS(st);
  } else if (WIFSIGNALED(st)) {
    printf("child pid=%d signaled=%d\n", (int)pid, WTERMSIG(st));
    return 128 + WTERMSIG(st);
  } else {
    printf("child pid=%d status=0x%x\n", (int)pid, st);
    return 1;
  }
}
