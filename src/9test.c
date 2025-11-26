#include "9/acme.h"
#include "9/errstr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main() {
  Acme *acme = acme_connect();
  if (acme == NULL) {
    fprintf(stderr, "acme_connect failed: %s\n", errstr9());
    return 1;
  }
  AcmeWin *win = acme_get_win(acme, "/tmp");
  if (win == NULL) {
    fprintf(stderr, "failed to open win: %s\n", errstr9());
    return 1;
  }
  if (acme_win_fmt_ctl(win, "get\n") < 0) {
    fprintf(stderr, "failed to write to ctl: %s\n", errstr9());
    return 1;
  }
  if (acme_win_fmt_tag(win, "Foo Bar Baz\n") < 0) {
    fprintf(stderr, "failed to write to tag: %s\n", errstr9());
    return 1;
  }
  char *s = acme_win_read_body(win);
  if (s == NULL) {
    fprintf(stderr, "failed to read body: %s\n", errstr9());
    return 1;
  }
  printf("%s", s);
  free(s);
  acme_close(acme);
  return 0;
}
