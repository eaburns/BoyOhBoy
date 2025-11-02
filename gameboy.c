#include "gameboy.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void fail(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  fprintf(stderr, "\n");
  abort();
}
