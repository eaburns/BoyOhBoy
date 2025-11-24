#include "errstr.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

static tss_t err_msg;
static once_flag once = ONCE_FLAG_INIT;

static void init_tss() {
  tss_create(&err_msg, free);
  tss_set(err_msg, strdup(""));
}

const char *errstr9() {
  call_once(&once, init_tss);
  return tss_get(err_msg);
}

void errstr9f(const char *fmt, ...) {
  call_once(&once, init_tss);

  va_list args;
  va_start(args, fmt);
  char small[1];
  int n = vsnprintf(small, sizeof(small), fmt, args);
  va_end(args);

  char *msg = calloc(n + 1, sizeof(*msg));
  va_start(args, fmt);
  int m = vsnprintf(msg, n + 1, fmt, args);
  va_end(args);
  if (m != n) {
    abort();
  }
  free(tss_get(err_msg));
  tss_set(err_msg, msg);
}
