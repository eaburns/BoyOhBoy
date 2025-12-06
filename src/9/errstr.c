#include "errstr.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pthread_key_t err_msg;
static pthread_once_t once = PTHREAD_ONCE_INIT;

static void init_tss() {
  pthread_key_create(&err_msg, free);
  pthread_setspecific(err_msg, strdup(""));
}

const char *errstr9() {
  pthread_once(&once, init_tss);
  return pthread_getspecific(err_msg);
}

void errstr9f(const char *fmt, ...) {
  pthread_once(&once, init_tss);

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
  free(pthread_getspecific(err_msg));
  pthread_setspecific(err_msg, msg);
}
