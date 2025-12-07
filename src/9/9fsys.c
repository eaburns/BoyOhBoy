#include "9fsys.h"

#include "9p.h"
#include "errstr.h"
#include "thread.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

struct file9 {
  Fsys9 *fsys;
  Fid9p fid;
  Mutex9 mtx;
  uint64_t offs;
  int iounit;
};

struct fsys9 {
  Client9p *client;
  Mutex9 mtx;
  Cond9 cnd;
  Fid9p root;
  bool closed;
  File9 files[MAX_OPEN_FILES];
};

Fsys9 *mount9_client(Client9p *c, const char *user) {
  Reply9p *r = wait9p(c, version9p(c, 1 << 20, VERSION_9P));
  if (r->type == R_ERROR_9P) {
    errstr9f("version9p failed: %s", r->error.message);
    free(r);
    goto err_version;
  }
  free(r);
  Fid9p root_fid = MAX_OPEN_FILES;
  r = wait9p(c, attach9p(c, root_fid, NOFID, user, ""));
  if (r->type == R_ERROR_9P) {
    errstr9f("attach9p failed: %s", r->error.message);
    free(r);
    goto err_attach;
  }
  free(r);
  Fsys9 *fsys = calloc(1, sizeof(*fsys));
  fsys->client = c;
  fsys->root = root_fid;
  mutex_init9(&fsys->mtx);
  cond_init9(&fsys->cnd);
  return fsys;
err_attach:
err_version:
  close9p(c);
  return NULL;
}

Fsys9 *mount9(const char *ns, const char *user) {
  Client9p *c = connect9p(ns);
  if (c == NULL) {
    errstr9f("connect9p failed");
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
  if (fsys == NULL) {
    return;
  }
  mutex_lock9(&fsys->mtx);
  fsys->closed = true;
  while (has_open_files(fsys)) {
    cond_wait9(&fsys->cnd, &fsys->mtx);
  }
  mutex_unlock9(&fsys->mtx);
  mutex_destroy9(&fsys->mtx);
  cond_destroy9(&fsys->cnd);
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
  mutex_lock9(&fsys->mtx);
  int fid = free_file(fsys);
  while (fid < 0) {
    cond_wait9(&fsys->cnd, &fsys->mtx);
    fid = free_file(fsys);
  }
  File9 *file = &fsys->files[fid];
  memset(file, 0, sizeof(*file));
  file->fsys = fsys;
  file->fid = fid;
  mutex_unlock9(&fsys->mtx);

  int max_elms = 1;
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
  free(elms);
  free(path_copy);
  if (r->type == R_ERROR_9P) {
    errstr9f("walk9p failed: %s", r->error.message);
    goto walk_err;
  }
  if (r->type != R_WALK_9P) {
    errstr9f("walk9p bad reply type: %d", r->type);
    goto walk_err;
  }
  if (r->walk.nqids != nelms) {
    errstr9f("%s not found", path);
    goto walk_err;
  }
  free(r);

  r = wait9p(fsys->client, open9p(fsys->client, file->fid, mode));
  if (r->type == R_ERROR_9P) {
    errstr9f("open9p failed: %s", r->error.message);
    goto open_err;
  }
  if (r->type != R_OPEN_9P) {
    errstr9f("open9p bad reply type: %d", r->type);
    goto open_err;
  }
  file->iounit = r->open.iounit;
  free(r);
  mutex_init9(&file->mtx);
  return file;
open_err:
  free(wait9p(fsys->client, clunk9p(fsys->client, file->fid)));
walk_err:
  free(r);
  mutex_lock9(&fsys->mtx);
  file->fsys = NULL;
  cond_broadcast9(&fsys->cnd);
  mutex_unlock9(&fsys->mtx);
  return NULL;
}

void close9(File9 *file) {
  Fsys9 *fsys = file->fsys;
  free(wait9p(fsys->client, clunk9p(file->fsys->client, file->fid)));
  mutex_lock9(&fsys->mtx);

  // Wait for any active read/write to finish.
  mutex_lock9(&file->mtx);
  mutex_unlock9(&file->mtx);
  mutex_destroy9(&file->mtx);

  file->fsys = NULL;
  file->fid = -1;
  cond_broadcast9(&fsys->cnd);
  mutex_unlock9(&fsys->mtx);
}

void rewind9(File9 *file) {
  mutex_lock9(&file->mtx);
  file->offs = 0;
  mutex_unlock9(&file->mtx);
}

int read9(File9 *file, int count, char *buf) {
  mutex_lock9(&file->mtx);
  if (count > file->iounit) {
    count = file->iounit;
  }
  Client9p *c = file->fsys->client;
  Reply9p *r = wait9p(c, read9p(c, file->fid, file->offs, count, buf));
  if (r->type == R_ERROR_9P) {
    errstr9f("read9p failed: %s", r->error.message);
    free(r);
    mutex_unlock9(&file->mtx);
    return -1;
  }
  if (r->type != R_READ_9P) {
    errstr9f("read9p bad reply type: %d", r->type);
    free(r);
    mutex_unlock9(&file->mtx);
    return -1;
  }
  file->offs += r->read.count;
  int total = r->read.count;
  free(r);
  mutex_unlock9(&file->mtx);
  return total;
}

int read9_full(File9 *file, int count, char *buf) {
  int total = 0;
  while (total < count) {
    int n = read9(file, count - total, buf + total);
    if (n == 0 && total == 0) {
      break;
    }
    if (n < 0) {
      return -1;
    }
    if (n == 0) {
      errstr9f("unexpected end-of-file");
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
      int size0 = size;
      size *= 2;
      buf = realloc(buf, size + 1);
      memset(buf + size0, '\0', size + 1 - size0);
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

struct read9_tag {
  Client9p *client;
  Tag9p tag;
};

Read9Tag *read9_async(File9 *file, unsigned long offs, int count, char *buf) {
  mutex_lock9(&file->mtx);
  if (count > file->iounit) {
    count = file->iounit;
  }
  Client9p *c = file->fsys->client;
  Tag9p tag9p = read9p(c, file->fid, offs, count, buf);
  mutex_unlock9(&file->mtx);
  if (tag9p < 0) {
    errstr9f("failed to initiate read");
    return NULL;
  }
  Read9Tag *tag = calloc(1, sizeof(*tag));
  tag->client = c;
  tag->tag = tag9p;
  return tag;
}

int read9_wait(Read9Tag *tag) {
  if (tag == NULL) {
    return -1;
  }
  Client9p *c = tag->client;
  Reply9p *r = wait9p(c, tag->tag);
  free(tag);
  if (r->type == R_ERROR_9P) {
    errstr9f("read9p failed: %s", r->error.message);
    free(r);
    return -1;
  }
  if (r->type != R_READ_9P) {
    errstr9f("read9p bad reply type: %d", r->type);
    free(r);
    return -1;
  }
  int total = r->read.count;
  free(r);
  return total;
}

Read9PollResult read9_poll(Read9Tag *tag) {
  if (tag == NULL) {
    errstr9f("tag is NULL");
    Read9PollResult result = {.done = true, .n = -1};
    return result;
  }
  Client9p *c = tag->client;
  Reply9p *r = poll9p(c, tag->tag);
  if (r == NULL) {
    Read9PollResult result = {.done = false, .n = 0};
    return result;
  }
  free(tag);
  if (r->type == R_ERROR_9P) {
    errstr9f("read9p failed: %s", r->error.message);
    free(r);
    Read9PollResult result = {.done = true, .n = -1};
    return result;
  }
  if (r->type != R_READ_9P) {
    errstr9f("read9p bad reply type: %d", r->type);
    free(r);
    Read9PollResult result = {.done = true, .n = -1};
    return result;
  }
  Read9PollResult result = {.done = true, .n = r->read.count};
  free(r);
  return result;
}

int write9(File9 *file, int count, const char *buf) {
  mutex_lock9(&file->mtx);
  int total = 0;
  while (count > 0) {
    int n = count;
    if (n > file->iounit) {
      n = file->iounit;
    }
    Client9p *c = file->fsys->client;
    Reply9p *r = wait9p(c, write9p(c, file->fid, file->offs, n, buf));
    if (r->type == R_ERROR_9P) {
      errstr9f("write9p failed: %s", r->error.message);
      free(r);
      break;
    }
    if (r->type != R_WRITE_9P) {
      errstr9f("write9p bad reply type: %d", r->type);
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
  mutex_unlock9(&file->mtx);
  return total;
}
