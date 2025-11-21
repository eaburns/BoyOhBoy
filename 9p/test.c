#include "9p.h"

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <threads.h>

// #define DEBUG(...) fprintf(stderr, __VA_ARGS__);
#define DEBUG(...)

#define FAIL(...)                                                              \
  do {                                                                         \
    fprintf(stderr, "%s: ", __func__);                                         \
    fprintf(stderr, __VA_ARGS__);                                              \
    abort();                                                                   \
  } while (0)

enum { HEADER_SIZE = sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint16_t) };

static Reply9p NO_REPLY;

typedef struct {
  FILE *socket;
  Client9p *client;
  thrd_t thrd;

  mtx_t mtx;
  cnd_t cnd;
  Reply9p *reply;
  Tag9p tag;
  bool done;
} TestServer;

static Client9p *connect_test_server(TestServer *);
static void server_will_reply(TestServer *, Reply9p *, Tag9p);
static void exchange_version(Client9p *c, TestServer *server);
static void close_test_server(TestServer *);
static void fprint_qid(FILE *f, Qid9p qid);

static void run_version9p_test() {
  DEBUG("running %s\n", __func__);
  TestServer server;
  Client9p *c = connect_test_server(&server);
  Tag9p tag = version9p(c, 100, "9P2000");

  static const int EXPECTED_MSIZE = 10;
  static const char *EXPECTED_VERSION = "9P2000";
  Reply9p reply = {
      .type = R_VERSION_9P,
      .version = {.msize = EXPECTED_MSIZE, .version = EXPECTED_VERSION},
  };
  server_will_reply(&server, &reply, tag);

  Reply9p *r = wait9p(c, tag);
  if (r->type != R_VERSION_9P) {
    FAIL("bad reply type: got %d, expected %d\n", r->type, R_VERSION_9P);
  }
  if (r->version.msize != EXPECTED_MSIZE) {
    FAIL("expected msize %d, got %d\n", EXPECTED_MSIZE, r->version.msize);
  }
  if (strcmp(r->version.version, EXPECTED_VERSION) != 0) {
    FAIL("expected version %s, got %s\n", EXPECTED_VERSION, r->version.version);
  }
  free(r);
  close_test_server(&server);
}

static void run_auth9p_test() {
  DEBUG("running %s\n", __func__);
  TestServer server;
  Client9p *c = connect_test_server(&server);
  exchange_version(c, &server);

  Tag9p tag = auth9p(c, 123, "uname", "aname");
  Reply9p reply = {
      .type = R_AUTH_9P,
      .auth = {.aqid = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}},
  };
  server_will_reply(&server, &reply, tag);

  Reply9p *r = wait9p(c, tag);
  if (r->type != R_AUTH_9P) {
    if (r->type == R_ERROR_9P) {
      FAIL("bad reply type: got %d (error %s), expected %d\n", r->type,
           r->error.message, R_AUTH_9P);
    }
    FAIL("bad reply type: got %d, expected %d\n", r->type, R_AUTH_9P);
  }
  char expected[13] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
  if (memcmp(r->auth.aqid, expected, sizeof(expected)) != 0) {
    fprintf(stderr, "received qid: ");
    fprint_qid(stderr, r->auth.aqid);
    FAIL("\nexpected qid: 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13\n");
  }
  free(r);
  close_test_server(&server);
}

static void run_attach9p_test() {
  DEBUG("running %s\n", __func__);
  TestServer server;
  Client9p *c = connect_test_server(&server);
  exchange_version(c, &server);

  Tag9p tag = attach9p(c, 567, 123, "uname", "aname");
  Reply9p reply = {
      .type = R_ATTACH_9P,
      .attach = {.qid = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}},
  };
  server_will_reply(&server, &reply, tag);

  Reply9p *r = wait9p(c, tag);
  if (r->type != R_ATTACH_9P) {
    if (r->type == R_ERROR_9P) {
      FAIL("bad reply type: got %d (error %s), expected %d\n", r->type,
           r->error.message, R_ATTACH_9P);
    }
    FAIL("bad reply type: got %d, expected %d\n", r->type, R_ATTACH_9P);
  }
  char expected[13] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
  if (memcmp(r->attach.qid, expected, sizeof(expected)) != 0) {
    fprintf(stderr, "received qid: ");
    fprint_qid(stderr, r->attach.qid);
    FAIL("\nexpected qid: 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13\n");
  }
  free(r);
  close_test_server(&server);
}

static void run_walk9p_test() {
  DEBUG("running %s\n", __func__);
  TestServer server;
  Client9p *c = connect_test_server(&server);
  exchange_version(c, &server);

  Tag9p tag = walk9p(c, 567, 123, 2, "a", "bc");
  Qid9p qids[] = {
      {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13},
      {13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1},
  };
  Reply9p reply = {
      .type = R_WALK_9P,
      .walk =
          {
              .nqids = 2,
              .qids = qids,
          },
  };
  server_will_reply(&server, &reply, tag);

  Reply9p *r = wait9p(c, tag);
  if (r->type != R_WALK_9P) {
    if (r->type == R_ERROR_9P) {
      FAIL("bad reply type: got %d (error %s), expected %d\n", r->type,
           r->error.message, R_WALK_9P);
    }
    FAIL("bad reply type: got %d, expected %d\n", r->type, R_WALK_9P);
  }
  if (r->walk.nqids != 2) {
    FAIL("expected 2 qids, got %d", r->walk.nqids);
  }
  char expected1[13] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
  if (memcmp(r->walk.qids[0], expected1, sizeof(expected1)) != 0) {
    fprintf(stderr, "received qid[0]: ");
    fprint_qid(stderr, r->walk.qids[0]);
    FAIL("\nexpected qid[0]: 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13\n");
  }
  char expected2[13] = {13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1};
  if (memcmp(r->walk.qids[1], expected2, sizeof(expected2)) != 0) {
    fprintf(stderr, "received qid[1]: ");
    fprint_qid(stderr, r->walk.qids[1]);
    FAIL("\nexpected qid[1]: 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1\n");
  }
  free(r);
  close_test_server(&server);
}

static void run_open9p_test() {
  DEBUG("running %s\n", __func__);
  TestServer server;
  Client9p *c = connect_test_server(&server);
  exchange_version(c, &server);

  Tag9p tag = open9p(c, 567, OREAD_9P);
  Reply9p reply = {
      .type = R_OPEN_9P,
      .open =
          {
              .qid = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13},
              .iounit = 1234,
          },
  };
  server_will_reply(&server, &reply, tag);

  Reply9p *r = wait9p(c, tag);
  if (r->type != R_OPEN_9P) {
    if (r->type == R_ERROR_9P) {
      FAIL("bad reply type: got %d (error %s), expected %d\n", r->type,
           r->error.message, R_OPEN_9P);
    }
    FAIL("bad reply type: got %d, expected %d\n", r->type, R_OPEN_9P);
  }
  char expected[13] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
  if (memcmp(r->open.qid, expected, sizeof(expected)) != 0) {
    fprintf(stderr, "received qid: ");
    fprint_qid(stderr, r->open.qid);
    FAIL("\nexpected qid: 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13\n");
  }
  if (r->open.iounit != 1234) {
    FAIL("got iounit %d, expected 1234\n", (int)r->open.iounit);
  }
  free(r);
  close_test_server(&server);
}

static void run_read9p_test() {
  DEBUG("running %s\n", __func__);
  TestServer server;
  Client9p *c = connect_test_server(&server);
  exchange_version(c, &server);

  char buf[16];
  Tag9p tag = read9p(c, 567, 10, sizeof(buf), buf);
  Reply9p reply = {
      .type = R_READ_9P,
      .read =
          {
              .count = sizeof(buf),
              .data = "123456789012345\0",
          },
  };
  server_will_reply(&server, &reply, tag);

  Reply9p *r = wait9p(c, tag);
  if (r->type != R_READ_9P) {
    if (r->type == R_ERROR_9P) {
      FAIL("bad reply type: got %d (error %s), expected %d\n", r->type,
           r->error.message, R_READ_9P);
    }
    FAIL("bad reply type: got %d, expected %d\n", r->type, R_READ_9P);
  }
  if (r->read.count != sizeof(buf)) {
    FAIL("got count %d, expected %d\n", (int)r->read.count, (int)sizeof(buf));
  }
  if (r->read.data != &buf[0]) {
    FAIL("got data buffer %p, expected %p\n", r->read.data, &buf[0]);
  }
  if (strncmp(buf, "123456789012345\0", sizeof(buf)) != 0) {
    FAIL("got data [%s], expected [123456789012345\\0]\n", buf);
  }
  free(r);
  close_test_server(&server);
}

static void run_write9p_test() {
  DEBUG("running %s\n", __func__);
  TestServer server;
  Client9p *c = connect_test_server(&server);
  exchange_version(c, &server);

  char buf[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
  Tag9p tag = write9p(c, 567, 10, sizeof(buf), buf);
  Reply9p reply = {
      .type = R_WRITE_9P,
      .read = {.count = sizeof(buf)},
  };
  server_will_reply(&server, &reply, tag);

  Reply9p *r = wait9p(c, tag);
  if (r->type != R_WRITE_9P) {
    if (r->type == R_ERROR_9P) {
      FAIL("bad reply type: got %d (error %s), expected %d\n", r->type,
           r->error.message, R_WRITE_9P);
    }
    FAIL("bad reply type: got %d, expected %d\n", r->type, R_WRITE_9P);
  }
  if (r->read.count != sizeof(buf)) {
    FAIL("got count %d, expected %d\n", (int)r->read.count, (int)sizeof(buf));
  }
  free(r);
  close_test_server(&server);
}

static void run_clunk9p_test() {
  DEBUG("running %s\n", __func__);
  TestServer server;
  Client9p *c = connect_test_server(&server);
  exchange_version(c, &server);

  Tag9p tag = clunk9p(c, 567);
  Reply9p reply = {.type = R_CLUNK_9P};
  server_will_reply(&server, &reply, tag);

  Reply9p *r = wait9p(c, tag);
  if (r->type != R_CLUNK_9P) {
    if (r->type == R_ERROR_9P) {
      FAIL("bad reply type: got %d (error %s), expected %d\n", r->type,
           r->error.message, R_CLUNK_9P);
    }
    FAIL("bad reply type: got %d, expected %d\n", r->type, R_CLUNK_9P);
  }
  free(r);
  close_test_server(&server);
}

static void run_wait9p_bad_tag_test() {
  DEBUG("running %s\n", __func__);
  TestServer server;
  Client9p *c = connect_test_server(&server);
  Reply9p *r = wait9p(c, -1);
  if (r->type != R_ERROR_9P) {
    FAIL("tag -1, expected error type, got %d\n", r->type);
  }
  if (strcmp(r->error.message, "bad tag") != 0) {
    FAIL("tag -1, expected \"bad tag\", got \"%s\"\n", r->error.message);
  }
  free(r);
  r = wait9p(c, QUEUE_SIZE);
  if (r->type != R_ERROR_9P) {
    FAIL("tag QUEUE_SIZE, expected error type, got %d\n", r->type);
  }
  if (strcmp(r->error.message, "bad tag") != 0) {
    FAIL("tag QUEUE_SIZE, expected \"bad tag\", got \"%s\"\n",
         r->error.message);
  }
  free(r);
  r = wait9p(c, QUEUE_SIZE - 1);
  if (r->type != R_ERROR_9P) {
    FAIL("tag unused, expected error type, got %d\n", r->type);
  }
  if (strcmp(r->error.message, "bad tag") != 0) {
    FAIL("tag unused, expected \"bad tag\", got \"%s\"\n", r->error.message);
  }
  free(r);
  close_test_server(&server);
}

static void run_poll9p_bad_tag_test() {
  DEBUG("running %s\n", __func__);
  TestServer server;
  Client9p *c = connect_test_server(&server);
  Reply9p *r = poll9p(c, -1);
  if (r->type != R_ERROR_9P) {
    FAIL("tag -1, expected error type, got %d\n", r->type);
  }
  if (strcmp(r->error.message, "bad tag") != 0) {
    FAIL("tag -1, expected \"bad tag\", got \"%s\"\n", r->error.message);
  }
  free(r);
  r = poll9p(c, QUEUE_SIZE);
  if (r->type != R_ERROR_9P) {
    FAIL("tag QUEUE_SIZE, expected error type, got %d\n", r->type);
  }
  if (strcmp(r->error.message, "bad tag") != 0) {
    FAIL("tag QUEUE_SIZE, expected \"bad tag\", got \"%s\"\n",
         r->error.message);
  }
  free(r);
  r = poll9p(c, QUEUE_SIZE - 1);
  if (r->type != R_ERROR_9P) {
    FAIL("tag unused, expected error type, got %d\n", r->type);
  }
  if (strcmp(r->error.message, "bad tag") != 0) {
    FAIL("tag unused, expected \"bad tag\", got \"%s\"\n", r->error.message);
  }
  free(r);
  close_test_server(&server);
}

static void run_reply_too_big_test() {
  DEBUG("running %s\n", __func__);
  TestServer server;
  Client9p *c = connect_test_server(&server);
  Tag9p tag = version9p(c, 10, "9P2000");

  // This will be larger than 10 bytes in total.
  Reply9p reply = {
      .type = R_VERSION_9P,
      .version = {.msize = 10, .version = "0123456789"},
  };
  server_will_reply(&server, &reply, tag);

  Reply9p *r = wait9p(c, tag);
  if (r->type != R_ERROR_9P) {
    FAIL("expected error, got %d\n", r->type);
  }
  if (strcmp(r->error.message, "connection closed") != 0) {
    FAIL("expected \"connection closed\", got \"%s\"\n", r->error.message);
  }
  free(r);
  close_test_server(&server);
}

static void run_send_too_big_test() {
  DEBUG("running %s\n", __func__);
  TestServer server;
  Client9p *c = connect_test_server(&server);
  Tag9p tag = version9p(c, 100, "9P2000");
  Reply9p reply1 = {
      .type = R_VERSION_9P,
      .version = {.msize = 10, .version = "0123456789"},
  };
  server_will_reply(&server, &reply1, tag);
  free(wait9p(c, tag));

  tag = auth9p(c, 5, "this is longer than 10 bytes", "and this is longer too");
  Reply9p *r = wait9p(c, tag);
  if (r->type != R_ERROR_9P) {
    FAIL("expected error, got %d\n", r->type);
  }
  if (strcmp(r->error.message, "message too big") != 0) {
    FAIL("expected \"message too big\", got \"%s\"\n", r->error.message);
  }
  free(r);
  close_test_server(&server);
}

static void run_bad_reply_tag_test() {
  DEBUG("running %s\n", __func__);
  TestServer server;
  Client9p *c = connect_test_server(&server);
  Tag9p tag = version9p(c, 100, "9P2000");

  Reply9p reply = {
      .type = R_VERSION_9P,
      .version = {.msize = 10, .version = "9P2000"},
  };
  server_will_reply(&server, &reply, tag + 1);

  Reply9p *r = wait9p(c, tag);
  if (r->type != R_ERROR_9P) {
    FAIL("expected error, got %d\n", r->type);
  }
  if (strcmp(r->error.message, "connection closed") != 0) {
    FAIL("expected \"connection closed\", got \"%s\"\n", r->error.message);
  }
  free(r);
  close_test_server(&server);
}

static void run_bad_reply_type_test() {
  DEBUG("running %s\n", __func__);
  TestServer server;
  Client9p *c = connect_test_server(&server);
  Tag9p tag = version9p(c, 100, "9P2000");

  Reply9p reply = {.type = R_AUTH_9P};
  server_will_reply(&server, &reply, tag);

  Reply9p *r = wait9p(c, tag);
  if (r->type != R_ERROR_9P) {
    FAIL("expected error, got %d\n", r->type);
  }
  if (strcmp(r->error.message, "connection closed") != 0) {
    FAIL("expected \"connection closed\", got \"%s\"\n", r->error.message);
  }
  free(r);
  close_test_server(&server);
}

static void run_receive_version_with_0byte() {
  DEBUG("running %s\n", __func__);
  TestServer server;
  Client9p *c = connect_test_server(&server);
  Tag9p tag = version9p(c, 100, "9P2000");

  Reply9p reply = {.type = R_VERSION_9P, .version = {.version = "XYZ"}};
  Reply9p *bad_reply = serialize_reply9p(&reply, tag);
  char *p = (char *)bad_reply + sizeof(Reply9p);
  p += HEADER_SIZE;
  p += sizeof(bad_reply->version.msize);
  p += sizeof(uint16_t); // string size field
  p[1] = 0;              // Add a null within the version string.

  server_will_reply(&server, bad_reply, tag);

  Reply9p *r = wait9p(c, tag);
  if (r->type != R_ERROR_9P) {
    FAIL("expected error, got %d\n", r->type);
  }
  if (strcmp(r->error.message, "connection closed") != 0) {
    FAIL("expected \"connection closed\", got \"%s\"\n", r->error.message);
  }
  free(bad_reply);
  free(r);
  close_test_server(&server);
}

static void run_receive_error_with_0byte() {
  DEBUG("running %s\n", __func__);
  TestServer server;
  Client9p *c = connect_test_server(&server);
  Tag9p tag = version9p(c, 100, "9P2000");

  Reply9p reply = {.type = R_ERROR_9P, .error = {.message = "XYZ"}};
  Reply9p *bad_reply = serialize_reply9p(&reply, tag);
  char *p = (char *)bad_reply + sizeof(Reply9p);
  p += HEADER_SIZE;
  p += sizeof(uint16_t); // string size field
  p[1] = 0;              // Add a null within the version string.

  server_will_reply(&server, bad_reply, tag);

  Reply9p *r = wait9p(c, tag);
  if (r->type != R_ERROR_9P) {
    FAIL("expected error, got %d\n", r->type);
  }
  if (strcmp(r->error.message, "connection closed") != 0) {
    FAIL("expected \"connection closed\", got \"%s\"\n", r->error.message);
  }
  free(bad_reply);
  free(r);
  close_test_server(&server);
}

static void run_read_response_too_big_test() {
  DEBUG("running %s\n", __func__);
  TestServer server;
  Client9p *c = connect_test_server(&server);
  exchange_version(c, &server);

  char buf[16];
  Tag9p tag = read9p(c, 567, 10, sizeof(buf), buf);
  Reply9p reply = {
      .type = R_READ_9P,
      .read =
          {
              .count = sizeof(buf) + 1, // Too big.
              .data = "123456789012345\0",
          },
  };
  server_will_reply(&server, &reply, tag);

  Reply9p *r = wait9p(c, tag);
  if (r->type != R_ERROR_9P) {
    FAIL("expected error, got %d\n", r->type);
  }
  if (strcmp(r->error.message, "connection closed") != 0) {
    FAIL("expected \"connection closed\", got \"%s\"\n", r->error.message);
  }
  free(r);
  close_test_server(&server);
}

static int server_thread(void *arg) {
  TestServer *server = arg;
  DEBUG("TEST SERVER: started\n");
  for (;;) {
    char s[4];
    if (fread(s, 1, sizeof(s), server->socket) != sizeof(s)) {
      if (feof(server->socket)) {
        break;
      }
      FAIL("test server: failed to read size");
    }
    int size = s[0] | (int)s[1] << 8 | (int)s[2] << 16 | (int)s[3] << 24;
    size -= sizeof(s);
    char *buf = calloc(1, size);
    DEBUG("TEST SERVER: reading %d bytes\n", size);
    if (fread(buf, 1, size, server->socket) != size) {
      FAIL("test server: failed to read message");
    }
    int type = buf[0];
    int tag = buf[1] | (int)buf[2] << 8;
    DEBUG("TEST SERVER: read message type %d tag %d\n", type, tag);
    free(buf);

    mtx_lock(&server->mtx);
    while (!server->done && server->reply == NULL) {
      cnd_wait(&server->cnd, &server->mtx);
    }
    if (server->done) {
      mtx_unlock(&server->mtx);
      break;
    }
    if (server->reply == &NO_REPLY) {
      mtx_unlock(&server->mtx);
      continue;
    }
    mtx_unlock(&server->mtx);

    Reply9p *reply = NULL;
    if (server->reply->internal_data_size == 0) {
      reply = serialize_reply9p(server->reply, server->tag);
    } else {
      reply = server->reply;
    }
    if (fwrite((char *)reply + sizeof(Reply9p), 1, reply->internal_data_size,
               server->socket) != reply->internal_data_size) {
      FAIL("server: failed to write reply\n");
    }
    DEBUG("TEST SERVER: sent message type %d\n", reply->type);
    if (server->reply->internal_data_size == 0) {
      free(reply);
    }
  }
  mtx_destroy(&server->mtx);
  fclose(server->socket);
  return 0;
}

static Client9p *connect_test_server(TestServer *server) {
  memset(server, 0, sizeof(*server));
  int sv[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
    FAIL("failed to create socket pair: %s\n", strerror(errno));
  }
  FILE *client = fdopen(sv[0], "r+");
  if (client == NULL) {
    FAIL("fdopen on client socket failed\n");
  }
  server->socket = fdopen(sv[1], "r+");
  if (server->socket == NULL) {
    FAIL("fdopen on server socket failed\n");
  }
  mtx_init(&server->mtx, mtx_plain);
  cnd_init(&server->cnd);
  thrd_create(&server->thrd, server_thread, server);
  server->client = connect_file9p(client);
  return server->client;
}

static void server_will_reply(TestServer *server, Reply9p *r, Tag9p tag) {
  mtx_lock(&server->mtx);
  server->reply = r;
  server->tag = tag;
  cnd_broadcast(&server->cnd);
  mtx_unlock(&server->mtx);
}

static void exchange_version(Client9p *c, TestServer *server) {
  Tag9p tag = version9p(c, 1024, "9P2000");
  Reply9p reply = {
      .type = R_VERSION_9P,
      .version = {.msize = 1024, .version = "9P2000"},
  };
  server_will_reply(server, &reply, tag);
  Reply9p *r = wait9p(c, tag);
  if (r->type != R_VERSION_9P) {
    abort();
  }
  free(r);
}

static void close_test_server(TestServer *server) {
  mtx_lock(&server->mtx);
  server->done = true;
  cnd_broadcast(&server->cnd);
  mtx_unlock(&server->mtx);
  close9p(server->client);
  thrd_join(server->thrd, NULL);
}

static void fprint_qid(FILE *f, Qid9p qid) {
  for (int i = 0; i < sizeof(Qid9p); i++) {
    if (i > 0) {
      fprintf(f, ", ");
    }
    fprintf(f, "%d", qid[i]);
  }
}

int main() {
  run_version9p_test();
  run_auth9p_test();
  run_attach9p_test();
  run_walk9p_test();
  run_open9p_test();
  run_read9p_test();
  run_write9p_test();
  run_clunk9p_test();
  run_wait9p_bad_tag_test();
  run_poll9p_bad_tag_test();
  run_reply_too_big_test();
  run_send_too_big_test();
  run_bad_reply_tag_test();
  run_bad_reply_type_test();
  run_receive_version_with_0byte();
  run_receive_error_with_0byte();
  run_read_response_too_big_test();
  return 0;
}
