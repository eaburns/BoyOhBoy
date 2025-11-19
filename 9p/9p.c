#include "9p.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

// #define DEBUG(...) fprintf(stderr, __VA_ARGS__)
#define DEBUG(...)

enum {
  T_VERSION_9P = 100,
  T_AUTH_9P = 102,
  T_ATTACH_9P = 104,
  T_WALK_9P = 110,
  T_OPEN_9P = 112,
  T_READ_9P = 116,

  HEADER_SIZE = sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint16_t),
  INIT_MAX_SEND_SIZE = 64,
};

typedef struct {
  bool in_use;
  bool flushed;
  uint8_t sent_type;
  Reply9p *reply;
  // Size and pointer passed to read9p().
  int read_buf_size;
  char *read_buf;
} QueueEntry;

struct Client9p {
  uint32_t max_send_size;
  uint32_t max_recv_size;

  thrd_t recv_thrd;

  mtx_t mtx;
  cnd_t cnd;
  FILE *f;
  bool closed;
  bool recv_thread_done;
  QueueEntry queue[QUEUE_SIZE];
};

extern FILE *dial_unix_socket(const char *path);
static int recv_thread(void *c);
static bool recv_header(Client9p *c, uint32_t *size, uint8_t *type,
                        uint16_t *tag);
static bool deserialize_reply(Reply9p *r, uint8_t type, const char *read_buf);
static Tag9p send(Client9p *c, char *msg);
static Tag9p send_with_buffer(Client9p *c, char *msg, int buf_size, char *buf);
static Reply9p *error_reply(const char *fmt, ...);
static bool queue_waiting(Client9p *c);
static bool queue_empty(Client9p *c);
static int free_queue_slot(Client9p *c);
static int string_size(const char *s);
static char *put1(char *p, uint8_t x);
static char *put_le2(char *p, uint16_t x);
static char *put_le4(char *p, uint32_t x);
static char *put_le8(char *p, uint64_t x);
static char *put_qid(char *p, Qid9p qid);
static char *put_string(char *p, const char *s);
static char *get1(char *p, uint8_t *x);
static char *get_le2(char *p, uint16_t *x);
static char *get_le4(char *p, uint32_t *x);
static char *get_le8(char *p, uint64_t *x);
static char *get_qid(char *p, Qid9p qid);
static char *get_data(char *p, uint16_t *size, const char **s);
static char *get_string_or_null(char *p, const char **s);

Client9p *connect9p(const char *path) {
  FILE *f = dial_unix_socket(path);
  if (f == NULL) {
    return NULL;
  }
  return connect_file9p(f);
}

Client9p *connect_file9p(FILE *f) {
  Client9p *c = calloc(1, sizeof(*c));
  c->f = f;
  c->max_send_size = INIT_MAX_SEND_SIZE;
  if (mtx_init(&c->mtx, mtx_plain) != thrd_success) {
    goto err;
  }
  if (cnd_init(&c->cnd) != thrd_success) {
    goto err_cnd;
  }
  if (thrd_create(&c->recv_thrd, recv_thread, c) != thrd_success) {
    goto err_thrd;
  }
  return c;
err_thrd:
  cnd_destroy(&c->cnd);
err_cnd:
  mtx_destroy(&c->mtx);
err:
  fclose(f);
  free(c);
  return NULL;
}

void close9p(Client9p *c) {
  DEBUG("close9p called\n");
  mtx_lock(&c->mtx);
  c->closed = true;
  cnd_broadcast(&c->cnd);
  DEBUG("close9p: waiting for everyone to close\n");
  // We only exit the loop with the lock held.
  // Wait for the waiters to go away and clean up.
  while (!queue_empty(c) || !c->recv_thread_done) {
    cnd_wait(&c->cnd, &c->mtx);
  }

  DEBUG("close9p: cleaning up\n");
  fclose(c->f);
  mtx_destroy(&c->mtx);
  cnd_destroy(&c->cnd);
  free(c);
}

static int recv_thread(void *arg) {
  Client9p *c = arg;
  for (;;) {
    DEBUG("recv_thread: waiting for queue\n");
    mtx_lock(&c->mtx);
    while (!c->closed && queue_waiting(c)) {
      cnd_wait(&c->cnd, &c->mtx);
    }
    if (c->closed) {
      DEBUG("recv_thread: got close\n");
      break;
    }

    DEBUG("recv_thread: receiving header\n");
    mtx_unlock(&c->mtx);
    uint32_t size;
    uint8_t type;
    uint16_t tag;
    bool ok = recv_header(c, &size, &type, &tag);
    mtx_lock(&c->mtx);

    if (!ok) {
      DEBUG("recv_thread: failed to receive the header\n");
      break;
    }
    DEBUG("recv_thread: got header\n");
    if (size > c->max_recv_size) {
      DEBUG("recv_thread: message too big: %d > %d\n", size, c->max_recv_size);
      break;
    }
    if (tag >= QUEUE_SIZE || c->queue[tag].sent_type == 0) {
      DEBUG("recv_thread: bad tag %d\n", tag);
      break;
    }
    QueueEntry *q = &c->queue[tag];
    if (type != R_ERROR_9P && type != R_FLUSH_9P && type != q->sent_type + 1) {
      DEBUG("recv_thread: bad response type, expected %d, got %d\n",
            q->sent_type + 1, type);
      break;
    }

    int body_size = size - HEADER_SIZE;
    if (type == R_READ_9P) {
      // For a read reply, we only read the header and count here.
      // The actual data will be read into the read9p() caller's buffer.
      body_size = sizeof(uint32_t); // the count
    }
    Reply9p *r = calloc(1, sizeof(Reply9p) + body_size);
    r->internal_data_size = body_size;
    DEBUG("recv_thread: receiving body %d bytes\n", body_size);
    mtx_unlock(&c->mtx);
    int n = fread((char *)r + sizeof(Reply9p), 1, body_size, c->f);
    mtx_lock(&c->mtx);

    if (n != body_size) {
      DEBUG("recv_thread: failed to read data\n");
      break;
    }
    if (!deserialize_reply(r, type, q->read_buf)) {
      DEBUG("recv_thread: failed deserialize reply\n");
      break;
    }

    if (type == R_READ_9P) {
      if (r->read.count > q->read_buf_size) {
        DEBUG("recv_thread: read reply count is too big %d > %d\n",
              r->read.count, q->read_buf_size);
        break;
      }
      DEBUG("recv_thread: reading %d bytes into read buffer\n", r->read.count);
      // Read the read reply data into the buffer passed to read9p().
      mtx_unlock(&c->mtx);
      int n = fread(q->read_buf, 1, r->read.count, c->f);
      mtx_lock(&c->mtx);
    }

    if (type == R_VERSION_9P) {
      c->max_send_size = r->version.msize;
    }
    DEBUG("recv_thread: finished reply for tag %d - broadcasting\n", tag);
    q->reply = r;
    cnd_broadcast(&c->cnd);
    mtx_unlock(&c->mtx);
    continue;
  }

  DEBUG("recv_thread: done\n");
  c->closed = true;
  c->recv_thread_done = true;
  cnd_broadcast(&c->cnd);
  mtx_unlock(&c->mtx);
  return 0;
}

static bool recv_header(Client9p *c, uint32_t *size, uint8_t *type,
                        uint16_t *tag) {
  char buf[HEADER_SIZE];
  int n = fread(buf, 1, sizeof(buf), c->f);
  if (n != sizeof(buf)) {
    DEBUG("recv_header: only got %d bytes\n", n);
    return false;
  }
  char *p = get_le4(buf, size);
  p = get1(p, type);
  p = get_le2(p, tag);
  return true;
}

Reply9p *serialize_reply9p(Reply9p *r, Tag9p tag) {
  int size = HEADER_SIZE;
  switch (r->type) {
  case R_VERSION_9P:
    size += sizeof(r->version.msize) + string_size(r->version.version);
    break;
  case R_AUTH_9P:
    size += sizeof(r->auth.aqid);
    break;
  case R_ERROR_9P:
    size += string_size(r->error.message);
    break;
  case R_ATTACH_9P:
    size += sizeof(r->attach.qid);
    break;
  case R_WALK_9P:
    size += sizeof(r->walk.nqids);
    size += sizeof(Qid9p) * r->walk.nqids;
    break;
  case R_OPEN_9P:
    size += sizeof(r->open.qid) + sizeof(r->open.iounit);
    break;
  case R_READ_9P:
    size += sizeof(r->read.count) + r->read.count;
    break;
  default:
    fprintf(stderr, "bad message type: %d\n", r->type);
    abort();
  }

  Reply9p *s = calloc(1, sizeof(Reply9p) + size);
  memcpy(s, r, sizeof(*r));
  s->internal_data_size = size;
  char *p = (char *)s + sizeof(Reply9p);
  p = put_le4(p, size);
  p = put1(p, r->type);
  p = put_le2(p, tag);

  switch (r->type) {
  case R_VERSION_9P:
    p = put_le4(p, r->version.msize);
    p = put_string(p, r->version.version);
    break;
  case R_AUTH_9P:
    p = put_qid(p, r->auth.aqid);
    break;
  case R_ERROR_9P:
    p = put_string(p, r->error.message);
    break;
  case R_ATTACH_9P:
    p = put_qid(p, r->attach.qid);
    break;
  case R_WALK_9P:
    p = put_le2(p, r->walk.nqids);
    for (int i = 0; i < r->walk.nqids; i++) {
      p = put_qid(p, r->walk.qids[i]);
    }
    break;
  case R_OPEN_9P:
    p = put_qid(p, r->open.qid);
    p = put_le4(p, r->open.iounit);
    break;
  case R_READ_9P:
    p = put_le4(p, r->read.count);
    memcpy(p, r->read.data, r->read.count);
    p += r->read.count;
    break;
  default:
    fprintf(stderr, "bad message type: %d\n", r->type);
    abort();
  }
  return s;
}

static bool deserialize_reply(Reply9p *r, uint8_t type, const char *read_buf) {
  r->type = type;
  char *start = (char *)r + sizeof(Reply9p);
  char *p = start;
  switch (r->type) {
  case R_VERSION_9P:
    p = get_le4(p, &r->version.msize);
    if (get_string_or_null(p, &r->version.version) == NULL) {
      return false;
    }
    break;
  case R_AUTH_9P:
    p = get_qid(p, r->auth.aqid);
    break;
  case R_ERROR_9P:
    if (get_string_or_null(p, &r->error.message) == NULL) {
      return false;
    }
    break;
  case R_ATTACH_9P:
    p = get_qid(p, r->attach.qid);
    break;
  case R_WALK_9P:
    p = get_le2(p, &r->walk.nqids);
    int bytes_left = r->internal_data_size - (p - start);
    if (bytes_left != sizeof(Qid9p) * r->walk.nqids) {
      DEBUG("expected %d bytes of Qid, got %d\n",
            (int)sizeof(Qid9p) * r->walk.nqids, bytes_left);
      return false;
    }
    r->walk.qids = (Qid9p *)p;
    break;
  case R_OPEN_9P:
    p = get_qid(p, r->open.qid);
    p = get_le4(p, &r->open.iounit);
    break;
  case R_READ_9P:
    p = get_le4(p, &r->read.count);
    r->read.data = read_buf;
    break;
  default:
    fprintf(stderr, "bad message type: %d\n", r->type);
    abort();
  }
  return true;
}

Tag9p version9p(Client9p *c, uint32_t msize, const char *version) {
  c->max_recv_size = msize;
  int size = HEADER_SIZE + sizeof(msize) + string_size(version);
  char *msg = calloc(1, size);
  char *p = put_le4(msg, size);
  p = put1(p, T_VERSION_9P);
  p = put_le2(p, 0); // Tag place holder
  p = put_le4(p, msize);
  p = put_string(p, version);
  return send(c, msg);
}

Tag9p auth9p(Client9p *c, Fid9p afid, const char *uname, const char *aname) {
  int size =
      HEADER_SIZE + sizeof(afid) + string_size(uname) + string_size(aname);
  char *msg = calloc(1, size);
  char *p = put_le4(msg, size);
  p = put1(p, T_AUTH_9P);
  p = put_le2(p, 0); // Tag place holder
  p = put_le4(p, afid);
  p = put_string(p, uname);
  p = put_string(p, aname);
  return send(c, msg);
}

Tag9p attach9p(Client9p *c, Fid9p fid, Fid9p afid, const char *uname,
               const char *aname) {
  int size = HEADER_SIZE + sizeof(fid) + sizeof(afid) + string_size(uname) +
             string_size(aname);
  char *msg = calloc(1, size);
  char *p = put_le4(msg, size);
  p = put1(p, T_ATTACH_9P);
  p = put_le2(p, 0); // Tag place holder
  p = put_le4(p, fid);
  p = put_le4(p, afid);
  p = put_string(p, uname);
  p = put_string(p, aname);
  return send(c, msg);
}

static int count(const char *path, char c) {
  int n = 0;
  for (const char *p = path; *p != '\0'; p++) {
    if (*p == c) {
      n++;
    }
  }
  return n;
}

Tag9p walk9p(Client9p *c, Fid9p fid, Fid9p new_fid, uint16_t nelms, ...) {
  va_list args;
  va_start(args, nelms);
  const char **elms = calloc(nelms, sizeof(char *));
  for (int i = 0; i < nelms; i++) {
    elms[i] = va_arg(args, const char *);
  }
  va_end(args);
  Tag9p tag = walk_array9p(c, fid, new_fid, nelms, elms);
  free(elms);
  return tag;
}

Tag9p walk_array9p(Client9p *c, Fid9p fid, Fid9p new_fid, uint16_t nelms,
                   const char **elms) {
  int size = HEADER_SIZE + sizeof(fid) + sizeof(new_fid) + sizeof(nelms);
  for (int i = 0; i < nelms; i++) {
    size += string_size(elms[i]);
  }
  char *msg = calloc(1, size);
  char *p = put_le4(msg, size);
  p = put1(p, T_WALK_9P);
  p = put_le2(p, 0); // Tag place holder
  p = put_le4(p, fid);
  p = put_le4(p, new_fid);
  p = put_le2(p, nelms);
  for (int i = 0; i < nelms; i++) {
    p = put_string(p, elms[i]);
  }
  return send(c, msg);
}

Tag9p open9p(Client9p *c, Fid9p fid, OpenMode9p mode) {
  int size = HEADER_SIZE + sizeof(fid) + sizeof(mode);
  char *msg = calloc(1, size);
  char *p = put_le4(msg, size);
  p = put1(p, T_OPEN_9P);
  p = put_le2(p, 0); // Tag place holder
  p = put_le4(p, fid);
  p = put1(p, mode);
  return send(c, msg);
}

Tag9p read9p(Client9p *c, Fid9p fid, uint64_t offs, uint32_t count, char *buf) {
  int size = HEADER_SIZE + sizeof(fid) + sizeof(offs) + sizeof(count);
  char *msg = calloc(1, size);
  char *p = put_le4(msg, size);
  p = put1(p, T_READ_9P);
  p = put_le2(p, 0); // Tag place holder
  p = put_le4(p, fid);
  p = put_le8(p, offs);
  p = put_le4(p, count);
  return send_with_buffer(c, msg, count, buf);
}

static Tag9p send(Client9p *c, char *msg) {
  return send_with_buffer(c, msg, 0, NULL);
}

static Tag9p send_with_buffer(Client9p *c, char *msg, int buf_size, char *buf) {
  mtx_lock(&c->mtx);
  Tag9p tag = free_queue_slot(c);
  while (!c->closed && tag < 0) {
    cnd_wait(&c->cnd, &c->mtx);
    tag = free_queue_slot(c);
  }
  if (c->closed) {
    DEBUG("send: closed before getting a tag\n");
    return -1;
  }
  uint8_t type;
  uint32_t size;
  get1(get_le4(msg, &size), &type);
  QueueEntry *q = &c->queue[tag];
  q->in_use = true;
  q->sent_type = type;
  if (type == T_READ_9P) {
    q->read_buf_size = buf_size;
    q->read_buf = buf;
  }

  if (size > c->max_send_size) {
    c->queue[tag].reply = error_reply("message too big");
    goto done;
  }

  put_le2((char *)msg + sizeof(uint32_t) + sizeof(uint8_t), tag);
  DEBUG("send: sending %d bytes\n", size);
  if (fwrite(msg, 1, size, c->f) != size) {
    DEBUG("send: failed to send\n");
    tag = -1;
    memset(&c->queue[tag], 0, sizeof(QueueEntry));
  }
done:
  cnd_broadcast(&c->cnd);
  mtx_unlock(&c->mtx);
  return tag;
}

Reply9p *wait9p(Client9p *c, Tag9p tag) {
  DEBUG("wait9p: waiting for reply for %d\n", tag);
  mtx_lock(&c->mtx);
  if (tag < 0 || tag >= QUEUE_SIZE || !c->queue[tag].in_use) {
    mtx_unlock(&c->mtx);
    return error_reply("bad tag");
  }
  while (!c->closed && c->queue[tag].reply == NULL) {
    cnd_wait(&c->cnd, &c->mtx);
  }
  Reply9p *r = c->queue[tag].reply;
  memset(&c->queue[tag], 0, sizeof(QueueEntry));
  if (c->closed) {
    DEBUG("wait9p: closed waiting for %d\n", tag);
    free(r);
    r = error_reply("connection closed");
  } else {
    DEBUG("wait9p: got reply for %d\n", tag);
  }
  cnd_broadcast(&c->cnd);
  mtx_unlock(&c->mtx);
  return r;
}

Reply9p *poll9p(Client9p *c, Tag9p tag) {
  DEBUG("poll9p: checking for a reply for %d\n", tag);
  mtx_lock(&c->mtx);
  if (tag < 0 || tag >= QUEUE_SIZE || !c->queue[tag].in_use) {
    mtx_unlock(&c->mtx);
    return error_reply("bad tag");
  }
  Reply9p *r = c->queue[tag].reply;
  if (c->closed || r != NULL) {
    memset(&c->queue[tag], 0, sizeof(QueueEntry));
    cnd_broadcast(&c->cnd);
  }
  if (r != NULL) {
    DEBUG("poll9p: got reply for %d\n", tag);
  }
  if (r == NULL && c->closed) {
    DEBUG("poll9p: closed waiting for %d\n", tag);
    r = error_reply("connection closed");
  }
  if (r == NULL) {
    DEBUG("poll9p: no reply yet for %d\n", tag);
  }
  mtx_unlock(&c->mtx);
  return r;
}

static Reply9p *error_reply(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  char b[1];
  int n = vsnprintf(b, 1, fmt, args);
  va_end(args);
  Reply9p *r = calloc(1, sizeof(Reply9p) + n + 1);
  r->type = R_ERROR_9P;
  va_start(args, fmt);
  int m = vsnprintf((char *)r + sizeof(Reply9p), n + 1, fmt, args);
  va_end(args);
  if (m != n) {
    abort(); // impossible
  }
  r->error.message = (char *)r + sizeof(Reply9p);
  return r;
}

static bool queue_waiting(Client9p *c) {
  for (int i = 0; i < QUEUE_SIZE; i++) {
    if (c->queue[i].in_use && c->queue[i].reply == NULL) {
      DEBUG("recv_thread: queue slot %d waiting\n", i);
      return false;
    }
  }
  return true;
}

static bool queue_empty(Client9p *c) {
  for (int i = 0; i < QUEUE_SIZE; i++) {
    if (c->queue[i].in_use) {
      DEBUG("recv_thread: queue slot %d in use\n", i);
      return false;
    }
  }
  return true;
}

static int free_queue_slot(Client9p *c) {
  for (int i = 0; i < QUEUE_SIZE; i++) {
    if (!c->queue[i].in_use) {
      return i;
    }
  }
  return -1;
}

static int string_size(const char *s) { return sizeof(uint16_t) + strlen(s); }

static char *put1(char *p, uint8_t x) {
  *p++ = x;
  return p;
}

static char *put_le2(char *p, uint16_t x) {
  *p++ = (x >> 0) & 0xFF;
  *p++ = (x >> 8) & 0xFF;
  return p;
}

static char *put_le4(char *p, uint32_t x) {
  *p++ = (x >> 0) & 0xFF;
  *p++ = (x >> 8) & 0xFF;
  *p++ = (x >> 16) & 0xFF;
  *p++ = (x >> 24) & 0xFF;
  return p;
}

static char *put_le8(char *p, uint64_t x) {
  *p++ = (x >> 0) & 0xFF;
  *p++ = (x >> 8) & 0xFF;
  *p++ = (x >> 16) & 0xFF;
  *p++ = (x >> 24) & 0xFF;
  *p++ = (x >> 32) & 0xFF;
  *p++ = (x >> 40) & 0xFF;
  *p++ = (x >> 48) & 0xFF;
  *p++ = (x >> 56) & 0xFF;
  return p;
}

static char *put_qid(char *p, Qid9p qid) {
  memcpy(p, qid, sizeof(Qid9p));
  p += sizeof(Qid9p);
  return p;
}

static char *put_string(char *p, const char *s) {
  int size = strlen(s);
  p = put_le2(p, size);
  memcpy(p, s, size);
  p += size;
  return p;
}

static char *get1(char *p, uint8_t *x) {
  *x = *p++;
  return p;
}

static char *get_le2(char *p, uint16_t *x) {
  *x = (uint16_t)*p++ << 0;
  *x |= (uint16_t)*p++ << 8;
  return p;
}

static char *get_le4(char *p, uint32_t *x) {
  *x = (uint32_t)*p++ << 0;
  *x |= (uint32_t)*p++ << 8;
  *x |= (uint32_t)*p++ << 16;
  *x |= (uint32_t)*p++ << 24;
  return p;
}

static char *get_le8(char *p, uint64_t *x) {
  *x = (uint64_t)*p++ << 0;
  *x |= (uint64_t)*p++ << 8;
  *x |= (uint64_t)*p++ << 16;
  *x |= (uint64_t)*p++ << 24;
  *x |= (uint64_t)*p++ << 32;
  *x |= (uint64_t)*p++ << 40;
  *x |= (uint64_t)*p++ << 48;
  *x |= (uint64_t)*p++ << 56;
  return p;
}

static char *get_qid(char *p, Qid9p qid) {
  memcpy(qid, p, sizeof(Qid9p));
  return p + sizeof(Qid9p);
}

static char *get_data(char *p, uint16_t *size, const char **s) {
  p = get_le2(p, size);
  *s = p;
  return p + *size;
}

static char *get_string_or_null(char *p, const char **s) {
  uint16_t size;
  p = get_le2(p, &size);
  if (memchr(p, '\0', size) != NULL) {
    return NULL;
  }
  p--;
  memmove(p, p + 1, size);
  p[size] = '\0';
  *s = p;
  return p + size + 1;
}
