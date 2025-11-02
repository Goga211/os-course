#include "common.h"
#include "string_utils.h"
#include <fcntl.h>
#include <unistd.h>


typedef struct {
    char   **cmd_argv;
    int      cmd_argc;
    const char *in_path;
    const char *out_path;
    int      syntax_error;
} parsed_t;

static int is_redirect_token(const char *tok) {
    return tok && (tok[0] == '<' || tok[0] == '>');
}

static const char* take_attached_filename(const char *tok) {

  if (!tok || !tok[0]) return NULL;

    if (tok[1] == '<' || tok[1] == '>') return NULL;

    if (tok[1] == '\0') return NULL;
    return tok + 1;
}

static parsed_t parse_redirs(char **argv) {
    parsed_t R = {0};
    int cap = 0; for (int i=0; argv && argv[i]; ++i) cap++;
    R.cmd_argv = (char**)calloc((size_t)cap + 1, sizeof(char*));
    if (!R.cmd_argv) DIE("calloc");

    for (int i=0; argv && argv[i]; ++i) {
        const char *tok = argv[i];

        if (tok && (strcmp(tok, "<") == 0 || strcmp(tok, ">") == 0)) {
            int is_in = (tok[0] == '<');

            const char *name = NULL;
            if (argv[i+1] && argv[i+1][0] != '<' && argv[i+1][0] != '>') {
                name = argv[i+1];
                i++;
            } else {
                R.syntax_error = 1;
                break;
            }

            if (is_in) {
                if (R.in_path) { R.syntax_error = 1; break; }
                R.in_path = name;
            } else {
                if (R.out_path) { R.syntax_error = 1; break; }
                R.out_path = name;
            }
            continue;
        }

        if (is_redirect_token(tok)) {
            int is_in = (tok[0] == '<');
            const char *name = take_attached_filename(tok);
            if (!name) { R.syntax_error = 1; break; }

            if (is_in) {
                if (R.in_path) { R.syntax_error = 1; break; }
                R.in_path = name;
            } else {
                if (R.out_path) { R.syntax_error = 1; break; }
                R.out_path = name;
            }
            continue;
        }

        R.cmd_argv[R.cmd_argc++] = argv[i];
    }

    R.cmd_argv[R.cmd_argc] = NULL;
    return R;
}

static int run_one_command_impl(char **argv){
  if (!argv || !argv[0]) return 0;

  parsed_t P = parse_redirs(argv);
  if (P.syntax_error || P.cmd_argc == 0) {
    fputs("Syntax error\n", stdout);
    fflush(stdout);
    free(P.cmd_argv);
    return 1;
  }

  int infd  = -1;
  int outfd = -1;

  if (P.in_path) {
    infd = open(P.in_path, O_RDONLY);
    if (infd < 0) {
      fputs("I/O error\n", stdout);
      fflush(stdout);
      free(P.cmd_argv);
      return 1;
    }
  }

  if (P.out_path) {
    outfd = open(P.out_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (outfd < 0) {
      if (infd >= 0) close(infd);
      fputs("I/O error\n", stdout);
      fflush(stdout);
      free(P.cmd_argv);
      return 1;
    }
  }

  struct timespec t0,t1; clock_gettime(CLOCK_MONOTONIC,&t0);
  pid_t pid = fork();
  if (pid < 0){
    perror("fork");
    if (infd  >= 0) close(infd);
    if (outfd >= 0) close(outfd);
    free(P.cmd_argv);
    return 127;
  }

  if (pid == 0){
    if (infd  >= 0) { if (dup2(infd,  STDIN_FILENO)  < 0) { perror("dup2"); _exit(1);} }
    if (outfd >= 0) { if (dup2(outfd, STDOUT_FILENO) < 0) { perror("dup2"); _exit(1);} }
    if (infd  >= 0) close(infd);
    if (outfd >= 0) close(outfd);

    execvp(P.cmd_argv[0], P.cmd_argv);
    perror("execvp");
    _exit(127);
  }

  if (infd  >= 0) close(infd);
  if (outfd >= 0) close(outfd);

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

  free(P.cmd_argv);
  return rc;
}

int vtsh_run_argv(char **argv){ return run_one_command_impl(argv); }
