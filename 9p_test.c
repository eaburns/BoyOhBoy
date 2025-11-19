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

  const char *user = getenv("USER");
  if (user == NULL) {
    fprintf(stderr, "failed to get $USER\n");
    return 1;
  }
  Fid9p root_fid = 123;
  r = wait9p(c, attach9p(c, root_fid, NOFID, user, ""));
  if (r->type == R_ERROR_9P) {
    fprintf(stderr, "main: attach9p failed: %s\n", r->error.message);
    return 1;
  }
  free(r);

  Fid9p body_fid = 456;
  r = wait9p(c, walk9p(c, root_fid, body_fid, 2, "new", "body"));
  if (r->type == R_ERROR_9P) {
    fprintf(stderr, "main: walk9p failed: %s\n", r->error.message);
    return 1;
  }
  free(r);

  r = wait9p(c, open9p(c, body_fid, OREAD_9P));
  if (r->type == R_ERROR_9P) {
    fprintf(stderr, "main: open9p failed: %s\n", r->error.message);
    return 1;
  }
  printf("iounit=%d\n", r->open.iounit);
  free(r);

  close9p(c);
  fprintf(stderr, "main: done\n");
  return 0;
}