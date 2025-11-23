#include "9fsys.h"

#include "9p.h"
#include "thrd.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

struct file9 {
  Fsys9 *fsys;
  Fid9p fid;
  mtx_t mtx;
  uint64_t offs;
  int iounit;
};

struct fsys9 {
  Client9p *client;
  mtx_t mtx;
  cnd_t cnd;
  Fid9p root;
  bool closed;
  File9 files[MAX_OPEN_FILES];
};

Fsys9 *mount9_client(Client9p *c, const char *user) {
  Reply9p *r = wait9p(c, version9p(c, 1 << 20, VERSION_9P));
  if (r->type == R_ERROR_9P) {
    fprintf(stderr, "version9p failed: %s\n", r->error.message);
    free(r);
    goto err_version;
  }
  free(r);
  Fid9p root_fid = MAX_OPEN_FILES;
  r = wait9p(c, attach9p(c, root_fid, NOFID, user, ""));
  if (r->type == R_ERROR_9P) {
    fprintf(stderr, "attach9p failed: %s\n", r->error.message);
    free(r);
    goto err_attach;
  }
  free(r);
  Fsys9 *fsys = calloc(1, sizeof(*fsys));
  fsys->client = c;
  fsys->root = root_fid;
  if (mtx_init(&fsys->mtx, mtx_plain) != thrd_success) {
    fprintf(stderr, "failed to initialize mtx\n");
    goto err_mtx;
  }
  if (cnd_init(&fsys->cnd) != thrd_success) {
    fprintf(stderr, "failed to initialize cnd\n");
    goto err_cnd;
  }
  return fsys;
err_cnd:
  mtx_destroy(&fsys->mtx);
err_mtx:
  free(fsys);
err_attach:
err_version:
  close9p(c);
  return NULL;
}

Fsys9 *mount9(const char *ns, const char *user) {
  Client9p *c = connect9p(ns);
  if (c == NULL) {
    fprintf(stderr, "failed to connect\n");
    return NULL;
  }
  return mount9_client(c, user);
}

static bool has_open_files(const Fsys9 *fsys) {
  for (int i = 0; i < MAX_OPEN_FILES; i++) {
    if (fsys->files[i].fsys != NULL) {
      return true;
    }
  }
  return false;
}

void unmount9(Fsys9 *fsys) {
  must_lock(&fsys->mtx);
  fsys->closed = true;
  while (has_open_files(fsys)) {
    must_wait(&fsys->cnd, &fsys->mtx);
  }
  mtx_destroy(&fsys->mtx);
  cnd_destroy(&fsys->cnd);
  close9p(fsys->client);
  free(fsys);
}

static int free_file(const Fsys9 *fsys) {
  for (int i = 0; i < MAX_OPEN_FILES; i++) {
    if (fsys->files[i].fsys == NULL) {
      return i;
    }
  }
  return -1;
}

File9 *open9(Fsys9 *fsys, const char *path, OpenMode9 mode) {
  must_lock(&fsys->mtx);
  int fid = free_file(fsys);
  while (fid < 0) {
    must_wait(&fsys->cnd, &fsys->mtx);
    fid = free_file(fsys);
  }
  File9 *file = &fsys->files[fid];
  memset(file, 0, sizeof(*file));
  file->fsys = fsys;
  file->fid = fid;
  must_unlock(&fsys->mtx);

  int max_elms = 0;
  for (const char *p = path; *p != '\0'; p++) {
    if (*p == '/') {
      max_elms++;
    }
  }
  int nelms = 0;
  const char **elms = calloc(max_elms, sizeof(*elms));
  char *path_copy = strdup(path);
  char *s = path_copy;
  for (char *p = path_copy; *p != '\0'; p++) {
    if (*p != '/') {
      continue;
    }
    *p = '\0';
    if (strcmp(s, ".") != 0 && *s != '\0') {
      elms[nelms++] = s;
    }
    s = p + 1;
  }
  if (strcmp(s, ".") != 0 && *s != '\0') {
    elms[nelms++] = s;
  }

  Fid9p dir = fsys->root;
  Reply9p *r = wait9p(fsys->client,
                      walk_array9p(fsys->client, fsys->root, fid, nelms, elms));
  if (r->type == R_ERROR_9P) {
    fprintf(stderr, "walk9p failed: %s\n", r->error.message);
    goto walk_err;
  }
  if (r->type != R_WALK_9P) {
    fprintf(stderr, "walk9p bad reply type: %d\n", r->type);
    goto walk_err;
  }
  free(r);

  r = wait9p(fsys->client, open9p(fsys->client, file->fid, mode));
  if (r->type == R_ERROR_9P) {
    fprintf(stderr, "open9p failed: %s\n", r->error.message);
    goto open_err;
  }
  if (r->type != R_OPEN_9P) {
    fprintf(stderr, "open9p bad reply type: %d\n", r->type);
    goto open_err;
  }
  file->iounit = r->open.iounit;
  free(r);
  if (mtx_init(&file->mtx, mtx_plain) != thrd_success) {
    fprintf(stderr, "failed to initialize mtx\n");
    goto mtx_err;
  }
  return file;
mtx_err:
open_err:
  free(wait9p(fsys->client, clunk9p(fsys->client, file->fid)));
walk_err:
  free(r);
  must_lock(&fsys->mtx);
  file->fsys = NULL;
  must_broadcast(&fsys->cnd);
  must_unlock(&fsys->mtx);
  return NULL;
}

void close9(File9 *file) {
  Fsys9 *fsys = file->fsys;
  free(wait9p(fsys->client, clunk9p(file->fsys->client, file->fid)));
  must_lock(&fsys->mtx);

  // Wait for any active read/write to finish.
  must_lock(&file->mtx);
  must_unlock(&file->mtx);
  mtx_destroy(&file->mtx);

  file->fsys = NULL;
  file->fid = -1;
  must_broadcast(&fsys->cnd);
  must_unlock(&fsys->mtx);
}

void rewind9(File9 *file) {
  must_lock(&file->mtx);
  file->offs = 0;
  must_unlock(&file->mtx);
}

int read9(File9 *file, int count, char *buf) {
  must_lock(&file->mtx);
  if (count > file->iounit) {
    count = file->iounit;
  }
  Client9p *c = file->fsys->client;
  Reply9p *r = wait9p(c, read9p(c, file->fid, file->offs, count, buf));
  if (r->type == R_ERROR_9P) {
    fprintf(stderr, "read9p failed: %s\n", r->error.message);
    free(r);
    must_unlock(&file->mtx);
    return -1;
  }
  if (r->type != R_READ_9P) {
    fprintf(stderr, "read9p bad reply type: %d\n", r->type);
    free(r);
    must_unlock(&file->mtx);
    return -1;
  }
  file->offs += r->read.count;
  int total = r->read.count;
  free(r);
  must_unlock(&file->mtx);
  return total;
}

int read9_full(File9 *file, int count, char *buf) {
  int total = 0;
  while (total < count) {
    int n = read9(file, count - total, buf + total);
    if (n == 0 && total == 0) {
      break;
    }
    if (n <= 0) {
      return -1;
    }
    total += n;
  }
  return total;
}

char *read9_all(File9 *file) {
  int size = 128;
  int offs = 0;
  char *buf = calloc(1, size + 1);
  for (;;) {
    if (size - offs < 128) {
      size *= 2;
      buf = realloc(buf, size + 1);
    }
    int n = read9(file, size - offs, buf + offs);
    if (n == 0) {
      break;
    }
    if (n < 0) {
      free(buf);
      return NULL;
    }
    offs += n;
  }
  return buf;
}

int write9(File9 *file, int count, const char *buf) {
  must_lock(&file->mtx);
  int total = 0;
  while (count > 0) {
    int n = count;
    if (n > file->iounit) {
      n = file->iounit;
    }
    Client9p *c = file->fsys->client;
    Reply9p *r = wait9p(c, write9p(c, file->fid, file->offs, n, buf));
    if (r->type == R_ERROR_9P) {
      fprintf(stderr, "write9p failed: %s\n", r->error.message);
      free(r);
      break;
    }
    if (r->type != R_WRITE_9P) {
      fprintf(stderr, "write9p bad reply type: %d\n", r->type);
      free(r);
      break;
    }
    if (r->write.count == 0) {
      // Don't spin writing nothing; this is a short-write.
      free(r);
      break;
    }
    file->offs += r->write.count;
    buf += r->write.count;
    total += r->write.count;
    count -= r->write.count;
    free(r);
  }
  must_unlock(&file->mtx);
  return total;
}
