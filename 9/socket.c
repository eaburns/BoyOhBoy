#include <errno.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>

FILE *dial_unix_socket(const char *path) {
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);

  struct sockaddr_un addr = {.sun_family = AF_UNIX};
  strncpy(addr.sun_path, path, sizeof(addr.sun_path));
  if (strlen(addr.sun_path) != strlen(path)) {
    fprintf(stderr, "path too long (max %zu)\n", strlen(addr.sun_path));
    errno = EINVAL;
    return NULL;
  }

  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    fprintf(stderr, "connect failed: %s\n", strerror(errno));
    return NULL;
  }

  return fdopen(fd, "r+");
}
