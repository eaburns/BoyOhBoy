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

  HEADER_SIZE = sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint16_t),
  INIT_MAX_SEND_SIZE = 64,
};

typedef struct {
  bool in_use;
  bool flushed;
  uint8_t sent_type;
  Reply9p *reply;
} QueueEntry;

struct Client9p {
  uint32_t max_send_size;
  uint32_t max_recv_size;

  thrd_t recv_thrd;

  mtx_t mtx;
  cnd_t cnd;
  FILE *f;
  bool closed;
  QueueEntry queue[QUEUE_SIZE];
};

extern FILE *dial_unix_socket(const char *path);
static int recv_thread(void *c);
static bool recv_header(Client9p *c, uint32_t *size, uint8_t *type,
                        uint16_t *tag);
static void deserialize_reply(Reply9p *r, uint8_t type);
static Tag9p send(Client9p *c, char *msg);
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
static char *get_qid(char *p, Qid9p *qid);
static char *get_data(char *p, uint16_t *size, const char **s);
static char *get_string(char *p, const char **s);

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
  mtx_unlock(&c->mtx);
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
      goto close;
    }
    DEBUG("recv_thread: got header\n");
    if (size > c->max_recv_size) {
      DEBUG("recv_thread: message too big: %d > %d\n", size, c->max_recv_size);
      goto close;
    }
    if (tag >= QUEUE_SIZE || c->queue[tag].sent_type == 0) {
      DEBUG("recv_thread: bad tag %d\n", tag);
      goto close;
    }
    QueueEntry *q = &c->queue[tag];
    if (type != R_ERROR_9P && type != R_FLUSH_9P && type != q->sent_type + 1) {
      DEBUG("recv_thread: bad response type, expected %d, got %d\n",
            q->sent_type + 1, type);
      goto close;
    }

    int body_size = size - HEADER_SIZE;
    Reply9p *r = calloc(1, sizeof(Reply9p) + body_size);
    r->internal_data_size = body_size;
    DEBUG("recv_thread: receiving body %d bytes\n", body_size);
    mtx_unlock(&c->mtx);
    int n = fread((char *)r + sizeof(Reply9p), 1, body_size, c->f);
    mtx_lock(&c->mtx);

    if (n != body_size) {
      DEBUG("recv_thread: failed to read data\n");
      goto close;
    }
    deserialize_reply(r, type);
    if (type == R_VERSION_9P) {
      c->max_send_size = r->version.msize;
    }
    DEBUG("recv_thread: got tag %d - broadcasting\n", tag);
    q->reply = r;
    cnd_broadcast(&c->cnd);
    mtx_unlock(&c->mtx);
    continue;

  close:
    c->closed = true;
    cnd_broadcast(&c->cnd);
    break;
  }

  DEBUG("recv_thread: waiting for everyone to close\n");
  // We only exit the loop with the lock held.
  // Wait for the waiters to go away and clean up.
  while (!queue_empty(c)) {
    cnd_wait(&c->cnd, &c->mtx);
  }
  DEBUG("recv_thread: cleaning up\n");
  fclose(c->f);
  mtx_destroy(&c->mtx);
  cnd_destroy(&c->cnd);
  free(c);
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
    size += sizeof(r->auth.aqid.bytes);
    break;
  case R_ERROR_9P:
    size += string_size(r->error.message);
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
  default:
    fprintf(stderr, "bad message type: %d\n", r->type);
    abort();
  }
  return s;
}

static void deserialize_reply(Reply9p *r, uint8_t type) {
  r->type = type;
  char *p = (char *)r + sizeof(Reply9p);
  switch (r->type) {
  case R_VERSION_9P:
    p = get_le4(p, &r->version.msize);
    p = get_string(p, &r->version.version);
    break;
  case R_AUTH_9P:
    p = get_qid(p, &r->auth.aqid);
    break;
  case R_ERROR_9P:
    get_string(p, &r->error.message);
    break;
  default:
    fprintf(stderr, "bad message type: %d\n", r->type);
    abort();
  }
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

static Tag9p send(Client9p *c, char *msg) {
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
  c->queue[tag].in_use = true;
  c->queue[tag].sent_type = type;

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
  Reply9p *r = calloc(1, sizeof(Reply9p) + n + 1);
  r->type = R_ERROR_9P;
  int m = vsnprintf((char *)r + sizeof(Reply9p), n + 1, fmt, args);
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
  memcpy(p, qid.bytes, sizeof(qid.bytes));
  p += sizeof(qid.bytes);
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

static char *get_qid(char *p, Qid9p *qid) {
  memcpy(qid->bytes, p, sizeof(qid->bytes));
  return p + sizeof(qid->bytes);
}

static char *get_data(char *p, uint16_t *size, const char **s) {
  p = get_le2(p, size);
  *s = p;
  return p + *size;
}

static char *get_string(char *p, const char **s) {
  uint16_t size;
  p = get_le2(p, &size);
  p--;
  memmove(p, p + 1, size);
  p[size] = '\0';
  *s = p;
  return p + size + 1;
}
