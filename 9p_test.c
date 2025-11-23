#include "9/acme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main() {
  Acme *acme = acme_connect();
  if (acme == NULL) {
    fprintf(stderr, "acme_connect failed\n");
    return 1;
  }

  AcmeWin *win = acme_get_win(acme, "/tmp");
  acme_win_write_ctl(win, "get\n");
  char *s = acme_win_read_body(win);
  printf("%s", s);
  free(s);

  acme_close(acme);
  return 0;
}