#include "errstr.h"
#include "thread.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ThreadLocal9 err_msg;
static Once9 once = ONCE9_INIT;

static void init_tss() {
  thread_local_init9(&err_msg, free);
}

const char *errstr9() {
  do_once9(&once, init_tss);
  char *msg = thread_local_get9(&err_msg);
  if (msg == NULL) {
    msg = strdup("OK");
  }
  thread_local_set9(&err_msg, msg);
  return msg;
}

void errstr9f(const char *fmt, ...) {
  do_once9(&once, init_tss);

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
  free(thread_local_get9(&err_msg));
  thread_local_set9(&err_msg, msg);
}
