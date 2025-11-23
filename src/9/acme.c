#include "acme.h"

#include "9fsys.h"
#include "thrd.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

// #define DEBUG(...) fprintf(stderr, __VA_ARGS__)
#define DEBUG(...)

enum { MAX_WINS = 10 };

struct acme {
  Fsys9 *fsys;
  File9 *index;

  mtx_t mtx;
  bool closed;
  AcmeWin *wins[MAX_WINS];
};

struct acme_win {
  Acme *acme;
  char *id;
  mtx_t mtx;
  File9 *ctl;
  File9 *addr;
  File9 *data;
  File9 *body;
};

static char *alloc_vsprintf(const char *fmt, va_list args);
static char *alloc_sprintf(const char *fmt, ...);

static Fsys9 *mount_acme() {
  const char *ns = getenv("NAMESPACE");
  if (ns == NULL) {
    fprintf(stderr, "acme_connect: no $NAMESPACE\n");
    return NULL;
  }
  const char *user = getenv("USER");
  if (user == NULL) {
    fprintf(stderr, "acme_connect: no $USER\n");
    return NULL;
  }
  const char *acme = "/acme";
  char *root_path = calloc(1, strlen(ns) + strlen(acme));
  strcpy(root_path, ns);
  strcpy(root_path + strlen(ns), acme);
  Fsys9 *fsys = mount9(root_path, user);
  free(root_path);
  return fsys;
}

Acme *acme_connect() {
  Acme *acme = calloc(1, sizeof(*acme));
  acme->fsys = mount_acme();
  if (acme->fsys == NULL) {
    goto mount_err;
  }
  acme->index = open9(acme->fsys, "index", OREAD_9);
  if (acme->index == NULL) {
    goto open_err;
  }
  if (mtx_init(&acme->mtx, mtx_plain) != thrd_success) {
    goto mtx_err;
  }
  return acme;
mtx_err:
  close9(acme->index);
open_err:
  unmount9(acme->fsys);
mount_err:
  free(acme);
  return NULL;
}

static void acme_release_win_with_lock(AcmeWin *win) {
  close9(win->ctl);
  close9(win->addr);
  close9(win->data);
  close9(win->body);
  free(win->id);
  mtx_destroy(&win->mtx);
  free(win);
}

void acme_close(Acme *acme) {
  must_lock(&acme->mtx);
  acme->closed = true;
  for (int i = 0; i < MAX_WINS; i++) {
    if (acme->wins[i] != NULL) {
      acme_release_win_with_lock(acme->wins[i]);
    }
  }
  must_unlock(&acme->mtx);
  close9(acme->index);
  unmount9(acme->fsys);
  mtx_destroy(&acme->mtx);
  free(acme);
}

static char *skip_to_space(char *s) {
  while (!isspace(*s)) {
    s++;
  }
  return s;
}

static char *skip_spaces(char *s) {
  while (isspace(*s)) {
    s++;
  }
  return s;
}

// Returns the first numeric entry in an index or ctl line, which is the win ID.
// The caller must free() the returned string.
static char *win_id_from_index_line(char *line) {
  char *id = skip_spaces(line);
  *skip_to_space(id) = '\0';
  return strdup(id);
}

// Returns the ID number of the win with the given name or NULL if there is not
// one. The caller must free() the returned string.
char *find_win_id(Acme *acme, const char *name) {
  rewind9(acme->index);
  char *index = read9_all(acme->index);
  if (index == NULL) {
    return NULL;
  }
  char *id = NULL;
  char *line_start = index;
  for (char *p = index; *p != '\0'; p++) {
    if (*p != '\n') {
      continue;
    }
    *p = '\0';
    // Win name is always preceeded by 5x 11 digit IDs with a space after each.
    char *win_name = line_start + 12 * 5;
    *skip_to_space(win_name) = '\0'; // null terminate the win_name.
    if (strcmp(win_name, name) != 0) {
      line_start = p + 1;
      continue;
    }
    id = win_id_from_index_line(line_start);
    break;
  }
  free(index);
  return id;
}

static int vfprint_file9(File9 *f, const char *fmt, va_list args) {
  char *s = alloc_vsprintf(fmt, args);
  rewind9(f);
  int len = strlen(s);
  int n = write9(f, len, s);
  free(s);
  return n != len ? -1 : n;
}

static int fprint_file9(File9 *f, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int n = vfprint_file9(f, fmt, args);
  va_end(args);
  return n;
}

// Returns either the ID of the win with the given name, or creates a new win,
// assigns it the name and returns its ID.
// The caller must free() the returned string.
static char *find_win_id_or_new(Acme *acme, const char *name) {
  char *id = find_win_id(acme, name);
  if (id != NULL) {
    return id;
  }
  File9 *f = open9(acme->fsys, "acme/new/ctl", ORDWR_9);
  if (f == NULL) {
    DEBUG("failed to open acme/new/ctl\n");
    return NULL;
  }
  int n = fprint_file9(f, "name %s\n", name);
  if (n < 0) {
    fprintf(stderr, "failed to write new win name\n");
    close9(f);
    return NULL;
  }
  rewind9(f);
  char *ctl = read9_all(f);
  if (ctl == NULL) {
    fprintf(stderr, "failed to read acme/new/ctl\n");
    close9(f);
    return NULL;
  }
  id = win_id_from_index_line(ctl);
  free(ctl);
  close9(f);
  return id;
}

static File9 *open_win_file(Acme *acme, const char *id, const char *file) {
  char *path = alloc_sprintf("acme/%s/%s", id, file);
  File9 *f = open9(acme->fsys, path, ORDWR_9);
  free(path);
  return f;
}

AcmeWin *acme_get_win(Acme *acme, const char *name) {
  must_lock(&acme->mtx);
  if (acme->closed) {
    goto err;
  }
  int i = 0;
  for (; i < MAX_WINS; i++) {
    if (acme->wins[i] == NULL) {
      break;
    }
  }
  if (i == MAX_WINS) {
    goto err;
  }
  char *id = find_win_id_or_new(acme, name);
  if (id == NULL) {
    fprintf(stderr, "failed to find or create win\n");
    goto err;
  }
  AcmeWin *win = calloc(1, sizeof(*win));
  win->id = id;
  win->ctl = open_win_file(acme, id, "ctl");
  if (win->ctl == NULL) {
    goto ctl_err;
  }
  win->addr = open_win_file(acme, id, "addr");
  if (win->addr == NULL) {
    goto addr_err;
  }
  win->data = open_win_file(acme, id, "data");
  if (win->data == NULL) {
    goto data_err;
  }
  win->body = open_win_file(acme, id, "body");
  if (win->body == NULL) {
    goto body_err;
  }
  if (mtx_init(&win->mtx, mtx_plain) != thrd_success) {
    goto mtx_err;
  }
  win->acme = acme;
  acme->wins[i] = win;
  must_unlock(&acme->mtx);
  return win;
mtx_err:
  close9(win->body);
body_err:
  close9(win->data);
data_err:
  close9(win->addr);
addr_err:
  close9(win->ctl);
ctl_err:
  free(win->id);
  free(win);
err:
  must_unlock(&acme->mtx);
  return NULL;
}

void acme_release_win(AcmeWin *win) {
  Acme *acme = win->acme;
  must_lock(&acme->mtx);
  for (int i = 0; i < MAX_WINS; i++) {
    if (acme->wins[i] == win) {
      acme->wins[i] = NULL;
      break;
    }
  }
  acme_release_win_with_lock(win);
  must_unlock(&acme->mtx);
}

int acme_win_write_ctl(AcmeWin *win, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  must_lock(&win->mtx);
  int n = vfprint_file9(win->ctl, fmt, args);
  must_unlock(&win->mtx);
  va_end(args);
  return n;
}

int acme_win_write_addr(AcmeWin *win, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  must_lock(&win->mtx);
  int n = vfprint_file9(win->addr, fmt, args);
  va_end(args);
  must_unlock(&win->mtx);
  return n;
}

int acme_win_write_data(AcmeWin *win, int size, const char *data) {
  must_lock(&win->mtx);
  rewind9(win->data);
  int n = write9(win->data, size, data);
  must_unlock(&win->mtx);
  return n;
}

int acme_win_write_body(AcmeWin *win, int size, const char *data) {
  must_lock(&win->mtx);
  rewind9(win->body);
  int n = write9(win->body, size, data);
  must_unlock(&win->mtx);
  return n;
}

char *acme_win_read_addr(AcmeWin *win) {
  must_lock(&win->mtx);
  rewind9(win->addr);
  char *s = read9_all(win->addr);
  must_unlock(&win->mtx);
  return s;
}

char *acme_win_read_data(AcmeWin *win) {
  must_lock(&win->mtx);
  rewind9(win->data);
  char *s = read9_all(win->data);
  must_unlock(&win->mtx);
  return s;
}

char *acme_win_read_body(AcmeWin *win) {
  must_lock(&win->mtx);
  rewind9(win->body);
  char *s = read9_all(win->body);
  must_unlock(&win->mtx);
  return s;
}

static char *alloc_vsprintf(const char *fmt, va_list args) {
  va_list args_copy;
  va_copy(args_copy, args);
  char small[1];
  int n = vsnprintf(small, sizeof(small), fmt, args);
  char *s = calloc(n + 1, sizeof(*s));
  if (vsnprintf(s, n + 1, fmt, args_copy) != n) {
    fprintf(stderr, "impossibel vsnprintf failure\n");
    abort();
  }
  va_end(args_copy);
  return s;
}

static char *alloc_sprintf(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  char *s = alloc_vsprintf(fmt, args);
  va_end(args);
  return s;
}
