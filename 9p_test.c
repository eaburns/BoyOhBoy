#include "9/9fsys.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main() {
  const char *ns = getenv("NAMESPACE");
  if (ns == NULL) {
    fprintf(stderr, "no $NAMESPACE\n");
    return 1;
  }
  const char *user = getenv("USER");
  if (user == NULL) {
    fprintf(stderr, "no $USER\n");
    return 1;
  }
  const char *acme = "/acme";
  char *root_path = calloc(1, strlen(ns) + strlen(acme));
  strcpy(root_path, ns);
  strcpy(root_path + strlen(ns), acme);
  fprintf(stderr, "%s: mounting %s as %s\n", __func__, root_path, user);
  Fsys9 *fsys = mount9(root_path, user);
  free(root_path);

  File9 *file = open9(fsys, "new/body", ORDWR_9);
  if (file == NULL) {
    fprintf(stderr, "%s: open9 failed\n", __func__);
    return 1;
  }

  const char *str = "Hello, World!\n";
  int n = write9(file, strlen(str), str);
  if (n != strlen(str)) {
    fprintf(stderr, "%s: write9 failed\n", __func__);
    return 1;
  }

  char buf[4096] = {};
  rewind9(file);
  n = read9(file, sizeof(buf), buf);
  if (n < 0) {
    fprintf(stderr, "%s: read9 failed\n", __func__);
    return 1;
  }
  printf("read %d bytes\n", n);
  for (int i = 0; i < n; i++) {
    char c = buf[i];
    printf("%c", c);
  }

  close9(file);
  unmount9(fsys);
  fprintf(stderr, "%s: done\n", __func__);
  return 0;
}