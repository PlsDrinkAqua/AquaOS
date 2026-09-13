#include <stdio.h>
int puts(const char *s) { return fputs(s, stdout) == EOF ? EOF : fputc('\n', stdout); }
