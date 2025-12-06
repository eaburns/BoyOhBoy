#include "acme.h"

#include "9fsys.h"
#include "errstr.h"
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

struct win {
  Acme *acme;
  char *id;
  mtx_t mtx;
  File9 *ctl;
  File9 *addr;
  File9 *data;
  File9 *body;
  File9 *tag;

  File9 *event;
  Read9Tag *event_read_tag;
  // Number of bytes currently in buf.
  int n;
  // Big enough to hold a full event message.
  // Each message has ≤256 bytes of data,
  // plus 2 characters of origin and type,
  // plus 4 decimal integers of unspecified size,
  // but surely less than 12 digits each, with a following space.
  // That totals 256+2+13*4 = 310, round to 512 for good measure.
  char buf[512];
};

static char *alloc_vsprintf(const char *fmt, va_list args);
static char *alloc_sprintf(const char *fmt, ...);

static Fsys9 *mount_acme() {
  const char *ns = getenv("NAMESPACE");
  if (ns == NULL) {
    errstr9f("acme_connect: no $NAMESPACE");
    return NULL;
  }
  const char *user = getenv("USER");
  if (user == NULL) {
    errstr9f("acme_connect: no $USER");
    return NULL;
  }
  const char *acme = "/acme";
  char *root_path = calloc(1, strlen(ns) + strlen(acme) + 1);
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
    errstr9f("failed to init mtx");
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

static void win_release_with_lock(AcmeWin *win) {
  close9(win->ctl);
  close9(win->addr);
  close9(win->data);
  close9(win->body);
  close9(win->tag);
  free(win->id);
  mtx_destroy(&win->mtx);
  free(win);
}

void acme_close(Acme *acme) {
  must_lock(&acme->mtx);
  acme->closed = true;
  for (int i = 0; i < MAX_WINS; i++) {
    if (acme->wins[i] != NULL) {
      win_release_with_lock(acme->wins[i]);
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
    return NULL;
  }
  int n = fprint_file9(f, "name %s\n", name);
  if (n < 0) {
    close9(f);
    return NULL;
  }
  rewind9(f);
  char *ctl = read9_all(f);
  if (ctl == NULL) {
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
    errstr9f("acme was closed");
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
  win->tag = open_win_file(acme, id, "tag");
  if (win->body == NULL) {
    goto tag_err;
  }
  if (mtx_init(&win->mtx, mtx_plain) != thrd_success) {
    errstr9f("failed to init mtx");
    goto mtx_err;
  }
  win->acme = acme;
  acme->wins[i] = win;
  must_unlock(&acme->mtx);
  return win;
mtx_err:
  close9(win->tag);
tag_err:
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

void win_release(AcmeWin *win) {
  Acme *acme = win->acme;
  must_lock(&acme->mtx);
  for (int i = 0; i < MAX_WINS; i++) {
    if (acme->wins[i] == win) {
      acme->wins[i] = NULL;
      break;
    }
  }
  win_release_with_lock(win);
  must_unlock(&acme->mtx);
}

int win_fmt_ctl(AcmeWin *win, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  must_lock(&win->mtx);
  int n = vfprint_file9(win->ctl, fmt, args);
  must_unlock(&win->mtx);
  va_end(args);
  return n;
}

int win_fmt_addr(AcmeWin *win, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  must_lock(&win->mtx);
  int n = vfprint_file9(win->addr, fmt, args);
  va_end(args);
  must_unlock(&win->mtx);
  return n;
}

int win_fmt_tag(AcmeWin *win, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  must_lock(&win->mtx);
  int n = vfprint_file9(win->tag, fmt, args);
  va_end(args);
  must_unlock(&win->mtx);
  return n;
}

int win_write_data(AcmeWin *win, int size, const char *data) {
  must_lock(&win->mtx);
  rewind9(win->data);
  int n = write9(win->data, size, data);
  must_unlock(&win->mtx);
  return n;
}

int win_write_body(AcmeWin *win, int size, const char *data) {
  must_lock(&win->mtx);
  rewind9(win->body);
  int n = write9(win->body, size, data);
  must_unlock(&win->mtx);
  return n;
}

char *win_read_addr(AcmeWin *win) {
  must_lock(&win->mtx);
  rewind9(win->addr);
  char *s = read9_all(win->addr);
  must_unlock(&win->mtx);
  return s;
}

char *win_read_data(AcmeWin *win) {
  must_lock(&win->mtx);
  rewind9(win->data);
  char *s = read9_all(win->data);
  must_unlock(&win->mtx);
  return s;
}

char *win_read_body(AcmeWin *win) {
  must_lock(&win->mtx);
  rewind9(win->body);
  char *s = read9_all(win->body);
  must_unlock(&win->mtx);
  return s;
}

char *win_read_tag(AcmeWin *win) {
  must_lock(&win->mtx);
  rewind9(win->tag);
  char *s = read9_all(win->tag);
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

static bool start_event_read(AcmeWin *win) {
  DEBUG("start_event_read\n");
  char *buf = win->buf + win->n;
  int n = sizeof(win->buf) - win->n;
  win->event_read_tag = read9_async(win->event, 0, n, buf);
  return win->event_read_tag != NULL;
}

bool win_start_events(AcmeWin *win) {
  must_lock(&win->mtx);
  if (win->event != NULL) {
    must_unlock(&win->mtx);
    errstr9f("events already started");
    return false;
  }
  DEBUG("opening event file\n");
  win->event = open_win_file(win->acme, win->id, "event");
  if (win->event == NULL) {
    DEBUG("open win file failed\n");
    must_unlock(&win->mtx);
    return false;
  }
  win->n = 0;
  if (!start_event_read(win)) {
    DEBUG("start_event_read failed\n");
    close9(win->event);
    win->event = NULL;
    win->event_read_tag = NULL;
    must_unlock(&win->mtx);
    return false;
  }
  must_unlock(&win->mtx);
  return true;
}

void win_stop_events(AcmeWin *win) {
  DEBUG("stopping events\n");
  must_lock(&win->mtx);
  if (win->event != NULL) {
    read9_wait(win->event_read_tag);
    win->event_read_tag = NULL;
    close9(win->event);
    win->event = NULL;
    win->n = 0;
  }
  must_unlock(&win->mtx);
}

static const int MALFORMED = -1;
static const int FETCH_MORE = -2;

static int get_event_int(AcmeWin *win, int *i) {
  int x = 0;
  char c = 0;
  do {
    if (*i >= win->n) {
      DEBUG("returning FETCH_MORE\n");
      return FETCH_MORE;
    }
    c = win->buf[(*i)++];
    if ('0' <= c && c <= '9') {
      c = c - '0';
      if (x > x * 10 + c) {
        DEBUG("%d *10 + %d overflows\n", x, c);
        return MALFORMED;
      }
      x = x * 10 + c;
    } else if (c != ' ') {
      DEBUG("%c is not a digit or space\n", c);
      return MALFORMED;
    }
  } while (c != ' ');
  return x;
}

static AcmeEvent *error_event(const char *msg) {
  int len = strlen(msg);
  AcmeEvent *error = calloc(1, sizeof(*error) + len + 1);
  error->count = len + 1;
  strncpy(error->data, msg, len);
  return error;
}

static AcmeEvent *deserialize_event(AcmeWin *win) {
  DEBUG("trying to deserialize: n=%d\n", win->n);
  int i = 0;
  if (i >= win->n) {
    DEBUG("not enough for anything: i=%d, n=%d\n", i, win->n);
    return NULL;
  }
  char origin = win->buf[i++];
  if (i >= win->n) {
    DEBUG("not enough for type: i=%d, n=%d\n", i, win->n);
    return NULL;
  }
  char type = win->buf[i++];
  int addr0 = get_event_int(win, &i);
  if (addr0 == MALFORMED) {
    goto error;
  }
  if (addr0 == FETCH_MORE) {
    DEBUG("not enough for addr0: i=%d, n=%d\n", i, win->n);
    return NULL;
  }
  int addr1 = get_event_int(win, &i);
  if (addr1 == MALFORMED) {
    goto error;
  }
  if (addr1 == FETCH_MORE) {
    DEBUG("not enough for addr1: i=%d, n=%d\n", i, win->n);
    return NULL;
  }
  int flags = get_event_int(win, &i);
  if (flags == MALFORMED) {
    goto error;
  }
  if (flags == FETCH_MORE) {
    DEBUG("not enough for flags: i=%d, n=%d\n", i, win->n);
    return NULL;
  }
  int count = get_event_int(win, &i);
  if (count == MALFORMED) {
    goto error;
  }
  DEBUG("count=%d\n", count);
  // +1 is for the terminating \n.
  if (count == FETCH_MORE || i + count + 1 > win->n) {
    DEBUG("not enough for count and \\n: i=%d, n=%d\n", i, win->n);
    return NULL;
  }
  if (win->buf[i + count] != '\n') {
    DEBUG("expected newline, got %c\n", win->buf[i + count]);
    goto error;
  }
  AcmeEvent *event = calloc(1, sizeof(*event) + count + 1);
  event->origin = origin;
  event->type = type;
  event->addr[0] = addr0;
  event->addr[1] = addr1;
  event->flags = flags;
  event->count = count;
  memcpy(event->data, win->buf + i, count);
  int event_size = i + count + 1;
  DEBUG("event size: %d bytes\n", event_size);
  memmove(win->buf, win->buf + event_size, win->n - event_size);
  win->n -= i + count + 1;
  return event;

error:
  return error_event("received malformed event");
}

AcmeEvent *win_poll_event(AcmeWin *win) {
  must_lock(&win->mtx);
  if (win->event == NULL) {
    must_unlock(&win->mtx);
    return NULL;
  }

retry:
  // We have data and aren't waiting for a pending read.
  // Try to deserialize the data.
  if (win->n > 0 && win->event_read_tag == NULL) {
    AcmeEvent *event = deserialize_event(win);
    if (event != NULL) {
      must_unlock(&win->mtx);
      return event;
    }
    DEBUG("failed to deserialize\n");
  }

  // We don't have enough data and aren't already waiting on a read.
  // Start a read and return.
  if (win->event_read_tag == NULL) {
    DEBUG("fetch more (n=%d)\n", win->n);
    if (start_event_read(win)) {
      must_unlock(&win->mtx);
      return NULL;
    }
    DEBUG("start_event_read failed\n");
    close9(win->event);
    win->event = NULL;
    must_unlock(&win->mtx);
    return error_event(errstr9());
  }

  // We don't have enough data, and have already started a read.
  // Check whether it's done.
  Read9PollResult poll_result = read9_poll(win->event_read_tag);
  if (!poll_result.done) {
    must_unlock(&win->mtx);
    return NULL;
  }
  win->event_read_tag = NULL;
  if (poll_result.n == 0) {
    DEBUG("read9_poll unexpected eof\n");
    close9(win->event);
    win->event = NULL;
    must_unlock(&win->mtx);
    return error_event("unexpected-end-of-file");
  }
  if (poll_result.n < 0) {
    DEBUG("read9_poll error: %s\n", errstr9());
    close9(win->event);
    win->event = NULL;
    must_unlock(&win->mtx);
    return error_event(errstr9());
  }

  // The read is done.
  // Accumulate the data, and let's try to deserialize it.
  win->n += poll_result.n;
  DEBUG("read9_poll got %d bytes, n=%d\n", poll_result.n, win->n);
  goto retry;
}

AcmeEvent *win_wait_event(AcmeWin *win) {
  must_lock(&win->mtx);
  if (win->event == NULL) {
    must_unlock(&win->mtx);
    return error_event("events not started");
  }

retry:
  if (win->n > 0 && win->event_read_tag == NULL) {
    AcmeEvent *event = deserialize_event(win);
    if (event != NULL) {
      must_unlock(&win->mtx);
      return event;
    }
  }
  if (win->event_read_tag == NULL) {
    DEBUG("fetch more (n=%d)\n", win->n);
    if (!start_event_read(win)) {
      DEBUG("start_event_read failed\n");
      close9(win->event);
      win->event = NULL;
      must_unlock(&win->mtx);
      return error_event(errstr9());
    }
  }
  int n = read9_wait(win->event_read_tag);
  win->event_read_tag = NULL;
  if (n == 0) {
    DEBUG("read9_wait unexpected eof\n");
    close9(win->event);
    win->event = NULL;
    must_unlock(&win->mtx);
    return error_event("unexpected-end-of-file");
  }
  if (n < 0) {
    DEBUG("read9_wait error: %s\n", errstr9());
    close9(win->event);
    win->event = NULL;
    must_unlock(&win->mtx);
    return error_event(errstr9());
  }
  win->n += n;
  DEBUG("read9_wait got %d bytes, n=%d\n", n, win->n);
  goto retry;
}

bool win_write_event(AcmeWin *win, AcmeEvent *event) {
  must_lock(&win->mtx);
  if (win->event == NULL) {
    must_unlock(&win->mtx);
    errstr9f("events not started");
    return false;
  }
  int n = fprint_file9(win->event, "%c%c%d %d \n", event->origin, event->type,
                       event->addr[0], event->addr[1]);
  must_unlock(&win->mtx);
  return n >= 0;
}
