// Needed for strerror_r 🙄.
#define _POSIX_C_SOURCE 200112L

#include "errstr.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

enum { ERRSIZE = 128 };

// Calls strerror_r and handles the possibility that it itself has an error 🙄.
static const char *errstr(int n, char buf[ERRSIZE]) {
  switch (strerror_r(n, buf, ERRSIZE)) {
  case 0:
    break;
  case EINVAL:
    strcpy(buf, "invalid errno");
    break;
  case ERANGE:
    strcpy(buf, "error message is too long");
    break;
  default:
    strcpy(buf, "non-POSIX-compliant strerror_r failure.");
    break;
  }
  return buf;
}

int dial_unix_socket(const char *path) {
  char errbuf[ERRSIZE];
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    errstr9f("failed to create socket: %s\n", errstr(errno, errbuf));
    return -1;
  }
  struct sockaddr_un addr = {.sun_family = AF_UNIX};
  strncpy(addr.sun_path, path, sizeof(addr.sun_path));
  if (strlen(addr.sun_path) != strlen(path)) {
    errstr9f("path too long (max %zu)\n", strlen(addr.sun_path));
    return -1;
  }
  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    errstr9f("connect failed: %s\n", errstr(errno, errbuf));
    return -1;
  }
  return fd;
}

int read_full(int fd, void *buf, int size) {
  char errbuf[ERRSIZE];
  int total = 0;
  uint8_t *p = buf;
  while (total < size) {
    int n = read(fd, p, size - total);
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n < 0) {
      errstr9f("read failed: %s", errstr(errno, errbuf));
      return -1;
    }
    if (total == 0 && n == 0) {
      errstr9f("end-of-file");
      break;
    }
    if (n == 0) {
      errstr9f("unexpected end-of-file");
      return -1;
    }
    p += n;
    total += n;
  }
  return total;
}

int write_full(int fd, void *buf, int size) {
  char errbuf[ERRSIZE];
  int total = 0;
  uint8_t *p = buf;
  while (total < size) {
    errno = 0;
    int n = write(fd, p, size - total);
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n < 0) {
      errstr9f("write failed: %s", errstr(errno, errbuf));
      return -1;
    }
    if (n == 0) {
      // It's a bit unclear what this means, but it's probably something bad.
      // The man page for Linux at least seems to indicate that this _may_ be an
      // error condition that sets errno. So we carefully set errno to zero
      // above so if this case does not set it, at least it's not some random
      // residual error.
      errstr9f("zero write: %s", errstr(errno, errbuf));
      return -1;
    }
    p += n;
    total += n;
  }
  return total;
}

void close_fd(int fd) { close(fd); }
