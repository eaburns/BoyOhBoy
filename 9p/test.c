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
static void close_test_server(TestServer *);

static void run_version9p_test() {
  TestServer server;
  Client9p *c = connect_test_server(&server);
  Tag9p tag = version9p(c, 100, "9P2000");

  static const int EXPECTED_MSIZE = 10;
  static const char *EXPECTED_VERSION = "9P2000";
  Reply9p r_version = {
      .type = R_VERSION_9P,
      .version = {.msize = EXPECTED_MSIZE, .version = EXPECTED_VERSION},
  };
  server_will_reply(&server, &r_version, tag);

  Reply9p *r = wait9p(c, tag);
  if (r->type != R_VERSION_9P) {
    FAIL("bad reply type: %d\n", r->type);
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

static void run_reply_too_big_test() {
  TestServer server;
  Client9p *c = connect_test_server(&server);
  Tag9p tag = version9p(c, 10, "9P2000");

  // This will be larger than 10 bytes in total.
  Reply9p r_version = {
      .type = R_VERSION_9P,
      .version = {.msize = 10, .version = "0123456789"},
  };
  server_will_reply(&server, &r_version, tag + 1);

  // Should close the connection.
  Reply9p *r = wait9p(c, tag);
  if (r->type != R_ERROR_9P) {
    FAIL("bad reply type: %d\n", r->type);
  }
  free(r);
  close_test_server(&server);
}

static void run_bad_reply_tag_test() {
  TestServer server;
  Client9p *c = connect_test_server(&server);
  Tag9p tag = version9p(c, 100, "9P2000");

  Reply9p r_version = {
      .type = R_VERSION_9P,
      .version = {.msize = 10, .version = "9P2000"},
  };
  server_will_reply(&server, &r_version, tag + 1);

  // Should close the connection.
  Reply9p *r = wait9p(c, tag);
  if (r->type != R_ERROR_9P) {
    FAIL("bad reply type: %d\n", r->type);
  }
  free(r);
  close_test_server(&server);
}

// Oops. Skip this for now, since
bool SKIP_run_bad_reply_type_test = true;

static void run_bad_reply_type_test() {
  if (SKIP_run_bad_reply_type_test) {
    fprintf(stderr, "skipping run_bad_reply_type_test until we have "
                    "implemented another message type\n");
    return;
  }
  TestServer server;
  Client9p *c = connect_test_server(&server);
  Tag9p tag = version9p(c, 100, "9P2000");

  Reply9p r_version = {.type = R_AUTH_9P};
  server_will_reply(&server, &r_version, tag);

  // Should close the connection.
  Reply9p *r = wait9p(c, tag);
  if (r->type != R_ERROR_9P) {
    FAIL("bad reply type: %d\n", r->type);
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

    Reply9p *reply = serialize_reply9p(server->reply, server->tag);
    if (fwrite((char *)reply + sizeof(Reply9p), 1, reply->internal_data_size,
               server->socket) != reply->internal_data_size) {
      FAIL("server: failed to write reply\n");
    }
    DEBUG("TEST SERVER: sent message type %d\n", reply->type);
    free(reply);
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

static void close_test_server(TestServer *server) {
  mtx_lock(&server->mtx);
  server->done = true;
  mtx_unlock(&server->mtx);
  close9p(server->client);
  thrd_join(server->thrd, NULL);
}

int main() {
  run_version9p_test();
  run_reply_too_big_test();
  run_bad_reply_tag_test();
  run_bad_reply_type_test();
  return 0;
}