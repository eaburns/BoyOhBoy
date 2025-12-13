#include "buffer.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void bprintf(Buffer *b, const char *fmt, ...) {
  char one[1];
  va_list args;
  va_start(args, fmt);
  int n = vsnprintf(one, 1, fmt, args);
  va_end(args);

  if (b->size + n + 1 >= b->cap) {
    if (b->cap == 0) {
      b->cap = 32;
    }
    while (b->size + n + 1 >= b->cap) {
      b->cap *= 2;
    }
    b->data = realloc(b->data, b->cap);
  }

  va_start(args, fmt);
  vsnprintf(b->data + b->size, n + 1, fmt, args);
  va_end(args);
  b->size += n;
}
