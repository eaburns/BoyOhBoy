#include "9p/9p.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main() {
  const char *ns = getenv("NAMESPACE");
  if (ns == NULL) {
    fprintf(stderr, "no NAMESPACE\n");
    return 1;
  }
  const char *acme = "/acme";
  char *path = calloc(1, strlen(ns) + strlen(acme));
  strcpy(path, ns);
  strcpy(path + strlen(ns), acme);
  fprintf(stderr, "main: connecting to [%s]\n", path);
  Client9p *c = connect9p(path);
  free(path);
  if (c == NULL) {
    perror("main: failed to connect");
    return 1;
  }

  fprintf(stderr, "main: version9p\n");
  Reply9p *r = wait9p(c, version9p(c, 1 << 20, VERSION_9P));
  if (r->type == R_ERROR_9P) {
    fprintf(stderr, "main: version9p failed: %s\n", r->error.message);
    return 1;
  }
  fprintf(stderr, "main: got msize %d, version %s\n", r->version.msize,
          r->version.version);
  free(r);

  close9p(c);
  fprintf(stderr, "main: done\n");
  return 0;
}