#pragma once
char*  str_trim(char *s);
char** split_whitespace(const char *s, int *argc_out);
void   free_argv(char **argv);
