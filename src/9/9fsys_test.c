#include "9fsys.h"

#include "9p.h"
#include "errstr.h"
#include <errno.h>
#include <signal.h>
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

static void must_join(thrd_t thrd) {
  if (thrd_join(thrd, NULL) != thrd_success) {
    abort();
  }
}

typedef struct {
  const char *test_name;
  Reply9p script[10];

  // These fields are set by connect_test_server.
  FILE *socket;
  Client9p *client;
  thrd_t thrd;
} TestServer;

static Client9p *connect_test_server(TestServer *server);

static void run_mount_unmount_test() {
  TestServer server = {
      .test_name = __func__,
      .script =
          {
              {
                  .type = R_VERSION_9P,
                  .version = {.msize = 1024, .version = "9P2000"},
              },
              {
                  .type = R_ATTACH_9P,
                  .attach = {},
              },
          },
  };
  Client9p *c = connect_test_server(&server);
  unmount9(mount9_client(c, "test_user"));
  must_join(server.thrd);
}

static void run_mount_version_error_test() {
  TestServer server = {
      .test_name = __func__,
      .script =
          {
              {
                  .type = R_ERROR_9P,
                  .error = {.message = "test error"},
              },
          },
  };
  Client9p *c = connect_test_server(&server);
  Fsys9 *fsys = mount9_client(c, "test_user");
  if (fsys != NULL) {
    FAIL("mount9_client returned non-NULL, expected NULL\n");
  }
  must_join(server.thrd);
}

static void run_mount_attach_error_test() {
  TestServer server = {
      .test_name = __func__,
      .script =
          {
              {
                  .type = R_VERSION_9P,
                  .version = {.msize = 1024, .version = "9P2000"},
              },
              {
                  .type = R_ERROR_9P,
                  .error = {.message = "test error"},
              },
          },
  };
  Client9p *c = connect_test_server(&server);
  Fsys9 *fsys = mount9_client(c, "test_user");
  if (fsys != NULL) {
    FAIL("mount9_client returned non-NULL, expected NULL\n");
  }
  must_join(server.thrd);
}

static void run_open_close_test() {
  Qid9p qids[2] = {};
  TestServer server = {
      .test_name = __func__,
      .script =
          {
              {
                  .type = R_VERSION_9P,
                  .version = {.msize = 1024, .version = "9P2000"},
              },
              {.type = R_ATTACH_9P},
              {
                  .type = R_WALK_9P,
                  .walk =
                      {
                          .nqids = 2,
                          .qids = qids,
                      },
              },
              {.type = R_OPEN_9P},
          },
  };
  Fsys9 *fsys = mount9_client(connect_test_server(&server), "test_user");
  File9 *file = open9(fsys, "/foo/bar", OREAD_9);
  if (file == NULL) {
    FAIL("open9 returned NULL: %s\n", errstr9());
  }
  close9(file);
  unmount9(fsys);
  must_join(server.thrd);
}

static void run_open_walk_error_test() {
  TestServer server = {
      .test_name = __func__,
      .script =
          {
              {
                  .type = R_VERSION_9P,
                  .version = {.msize = 1024, .version = "9P2000"},
              },
              {.type = R_ATTACH_9P},
              {
                  .type = R_ERROR_9P,
                  .error = {.message = "test error"},
              },
          },
  };
  Fsys9 *fsys = mount9_client(connect_test_server(&server), "test_user");
  File9 *file = open9(fsys, "/foo/bar", OREAD_9);
  if (file != NULL) {
    FAIL("open9 returned non-NULL, expected NULL\n");
  }
  unmount9(fsys);
  must_join(server.thrd);
}

static void run_open_walk_short_test() {
  Qid9p qids[1] = {};
  TestServer server = {
      .test_name = __func__,
      .script =
          {
              {
                  .type = R_VERSION_9P,
                  .version = {.msize = 1024, .version = "9P2000"},
              },
              {.type = R_ATTACH_9P},
              {
                  .type = R_WALK_9P,
                  .walk =
                      {
                          .nqids = 1, // expected 2
                          .qids = qids,
                      },
              },
          },
  };
  Fsys9 *fsys = mount9_client(connect_test_server(&server), "test_user");
  File9 *file = open9(fsys, "/foo/bar", OREAD_9);
  if (file != NULL) {
    FAIL("open9 returned non-NULL, expected NULL\n");
  }
  if (strcmp(errstr9(), "/foo/bar not found") != 0) {
    FAIL("open9 expected \"/foo/bar not found\", got \"%s\"\n", errstr9());
  }
  unmount9(fsys);
  must_join(server.thrd);
}

static void run_open_open_error_test() {
  Qid9p qids[2] = {};
  TestServer server = {
      .test_name = __func__,
      .script =
          {
              {
                  .type = R_VERSION_9P,
                  .version = {.msize = 1024, .version = "9P2000"},
              },
              {.type = R_ATTACH_9P},
              {
                  .type = R_WALK_9P,
                  .walk =
                      {
                          .nqids = 2,
                          .qids = qids,
                      },
              },
              {
                  .type = R_ERROR_9P,
                  .error = {.message = "test error"},
              },
          },
  };
  Fsys9 *fsys = mount9_client(connect_test_server(&server), "test_user");
  File9 *file = open9(fsys, "/foo/bar", OREAD_9);
  if (file != NULL) {
    FAIL("open9 returned non-NULL, expected NULL\n");
  }
  unmount9(fsys);
  must_join(server.thrd);
}

static void run_read_test() {
  Qid9p qids[2] = {};
  TestServer server = {
      .test_name = __func__,
      .script =
          {
              {
                  .type = R_VERSION_9P,
                  .version = {.msize = 1024, .version = "9P2000"},
              },
              {.type = R_ATTACH_9P},
              {
                  .type = R_WALK_9P,
                  .walk =
                      {
                          .nqids = 2,
                          .qids = qids,
                      },
              },
              {
                  .type = R_OPEN_9P,
                  .open = {.iounit = 100},
              },
              {
                  .type = R_READ_9P,
                  .read = {.count = 4, .data = "1234"},
              },
          },
  };
  Fsys9 *fsys = mount9_client(connect_test_server(&server), "test_user");
  File9 *file = open9(fsys, "/foo/bar", OREAD_9);
  if (file == NULL) {
    FAIL("open9 returned NULL\n");
  }
  char buf[5] = {};
  int n = read9(file, 4, buf);
  if (n != 4) {
    FAIL("read9 returned %d, expected 4\n", n);
  }
  if (strcmp(buf, "1234") != 0) {
    FAIL("read9, buf is %s, expected 1234\n", buf);
  }
  close9(file);
  unmount9(fsys);
  must_join(server.thrd);
}

static void run_short_read_test() {
  Qid9p qids[2] = {};
  TestServer server = {
      .test_name = __func__,
      .script =
          {
              {
                  .type = R_VERSION_9P,
                  .version = {.msize = 1024, .version = "9P2000"},
              },
              {.type = R_ATTACH_9P},
              {
                  .type = R_WALK_9P,
                  .walk =
                      {
                          .nqids = 2,
                          .qids = qids,
                      },
              },
              {
                  .type = R_OPEN_9P,
                  .open = {.iounit = 100},
              },
              {
                  .type = R_READ_9P,
                  .read = {.count = 4, .data = "1234"},
              },
          },
  };
  Fsys9 *fsys = mount9_client(connect_test_server(&server), "test_user");
  File9 *file = open9(fsys, "/foo/bar", OREAD_9);
  if (file == NULL) {
    FAIL("open9 returned NULL\n");
  }
  char buf[100] = {};
  int n = read9(file, 100, buf);
  if (n != 4) {
    FAIL("read9 returned %d, expected 4\n", n);
  }
  if (strcmp(buf, "1234") != 0) {
    FAIL("read9, buf is %s, expected 1234\n", buf);
  }
  close9(file);
  unmount9(fsys);
  must_join(server.thrd);
}

static void run_read_error_test() {
  Qid9p qids[2] = {};
  TestServer server = {
      .test_name = __func__,
      .script =
          {
              {
                  .type = R_VERSION_9P,
                  .version = {.msize = 1024, .version = "9P2000"},
              },
              {.type = R_ATTACH_9P},
              {
                  .type = R_WALK_9P,
                  .walk =
                      {
                          .nqids = 2,
                          .qids = qids,
                      },
              },
              {
                  .type = R_OPEN_9P,
                  .open = {.iounit = 100},
              },
              {
                  .type = R_ERROR_9P,
                  .error = {.message = "test error"},
              },
          },
  };
  Fsys9 *fsys = mount9_client(connect_test_server(&server), "test_user");
  File9 *file = open9(fsys, "/foo/bar", OREAD_9);
  if (file == NULL) {
    FAIL("open9 returned NULL\n");
  }
  char buf[100] = {};
  int n = read9(file, 100, buf);
  if (n >= 0) {
    FAIL("read9 returned %d, expected -1\n", n);
  }
  close9(file);
  unmount9(fsys);
  must_join(server.thrd);
}

static void run_read_full_test() {
  Qid9p qids[2] = {};
  TestServer server = {
      .test_name = __func__,
      .script =
          {
              {
                  .type = R_VERSION_9P,
                  .version = {.msize = 1024, .version = "9P2000"},
              },
              {.type = R_ATTACH_9P},
              {
                  .type = R_WALK_9P,
                  .walk =
                      {
                          .nqids = 2,
                          .qids = qids,
                      },
              },
              {
                  .type = R_OPEN_9P,
                  .open = {.iounit = 100},
              },
              {
                  .type = R_READ_9P,
                  .read = {.count = 1, .data = "1"},
              },
              {
                  .type = R_READ_9P,
                  .read = {.count = 1, .data = "2"},
              },
              {
                  .type = R_READ_9P,
                  .read = {.count = 1, .data = "3"},
              },
              {
                  .type = R_READ_9P,
                  .read = {.count = 1, .data = "4"},
              },
          },
  };
  Fsys9 *fsys = mount9_client(connect_test_server(&server), "test_user");
  File9 *file = open9(fsys, "/foo/bar", OREAD_9);
  if (file == NULL) {
    FAIL("open9 returned NULL\n");
  }
  char buf[5] = {};
  int n = read9_full(file, 4, buf);
  if (n != 4) {
    FAIL("read9_full returned %d, expected 4\n", n);
  }
  if (strcmp(buf, "1234") != 0) {
    FAIL("read9_full, buf is %s, expected 1234\n", buf);
  }
  close9(file);
  unmount9(fsys);
  must_join(server.thrd);
}

static void run_read_full_eof_test() {
  Qid9p qids[2] = {};
  TestServer server = {
      .test_name = __func__,
      .script =
          {
              {
                  .type = R_VERSION_9P,
                  .version = {.msize = 1024, .version = "9P2000"},
              },
              {.type = R_ATTACH_9P},
              {
                  .type = R_WALK_9P,
                  .walk =
                      {
                          .nqids = 2,
                          .qids = qids,
                      },
              },
              {
                  .type = R_OPEN_9P,
                  .open = {.iounit = 100},
              },
              {
                  .type = R_READ_9P,
                  .read = {.count = 0, .data = NULL},
              },
          },
  };
  Fsys9 *fsys = mount9_client(connect_test_server(&server), "test_user");
  File9 *file = open9(fsys, "/foo/bar", OREAD_9);
  if (file == NULL) {
    FAIL("open9 returned NULL\n");
  }
  char buf[5] = {};
  int n = read9_full(file, 4, buf);
  if (n != 0) {
    FAIL("read9_full returned %d, expected 0\n", n);
  }
  close9(file);
  unmount9(fsys);
  must_join(server.thrd);
}

static void run_read_full_unexpected_eof_test() {
  Qid9p qids[2] = {};
  TestServer server = {
      .test_name = __func__,
      .script =
          {
              {
                  .type = R_VERSION_9P,
                  .version = {.msize = 1024, .version = "9P2000"},
              },
              {.type = R_ATTACH_9P},
              {
                  .type = R_WALK_9P,
                  .walk =
                      {
                          .nqids = 2,
                          .qids = qids,
                      },
              },
              {
                  .type = R_OPEN_9P,
                  .open = {.iounit = 100},
              },
              {
                  .type = R_READ_9P,
                  .read = {.count = 1, .data = "1"},
              },
              {
                  .type = R_READ_9P,
                  .read = {.count = 0, .data = NULL},
              },
          },
  };
  Fsys9 *fsys = mount9_client(connect_test_server(&server), "test_user");
  File9 *file = open9(fsys, "/foo/bar", OREAD_9);
  if (file == NULL) {
    FAIL("open9 returned NULL\n");
  }
  char buf[5] = {};
  int n = read9_full(file, 4, buf);
  if (n >= 0) {
    FAIL("read9_full returned %d, expected -1\n", n);
  }
  close9(file);
  unmount9(fsys);
  must_join(server.thrd);
}

static void run_read_full_error_test() {
  Qid9p qids[2] = {};
  TestServer server = {
      .test_name = __func__,
      .script =
          {
              {
                  .type = R_VERSION_9P,
                  .version = {.msize = 1024, .version = "9P2000"},
              },
              {.type = R_ATTACH_9P},
              {
                  .type = R_WALK_9P,
                  .walk =
                      {
                          .nqids = 2,
                          .qids = qids,
                      },
              },
              {
                  .type = R_OPEN_9P,
                  .open = {.iounit = 100},
              },
              {
                  .type = R_READ_9P,
                  .read = {.count = 1, .data = "1"},
              },
              {
                  .type = R_ERROR_9P,
                  .error = {.message = "test error"},
              },
          },
  };
  Fsys9 *fsys = mount9_client(connect_test_server(&server), "test_user");
  File9 *file = open9(fsys, "/foo/bar", OREAD_9);
  if (file == NULL) {
    FAIL("open9 returned NULL\n");
  }
  char buf[5] = {};
  int n = read9_full(file, 4, buf);
  if (n >= 0) {
    FAIL("read9_full returned %d, expected -1\n", n);
  }
  close9(file);
  unmount9(fsys);
  must_join(server.thrd);
}

static void run_read9_wait_test() {
  Qid9p qids[2] = {};
  TestServer server = {
      .test_name = __func__,
      .script =
          {
              {
                  .type = R_VERSION_9P,
                  .version = {.msize = 1024, .version = "9P2000"},
              },
              {.type = R_ATTACH_9P},
              {
                  .type = R_WALK_9P,
                  .walk =
                      {
                          .nqids = 2,
                          .qids = qids,
                      },
              },
              {
                  .type = R_OPEN_9P,
                  .open = {.iounit = 100},
              },
              {
                  .type = R_READ_9P,
                  .read = {.count = 4, .data = "1234"},
              },
          },
  };
  Fsys9 *fsys = mount9_client(connect_test_server(&server), "test_user");
  File9 *file = open9(fsys, "/foo/bar", OREAD_9);
  if (file == NULL) {
    FAIL("open9 returned NULL\n");
  }
  char buf[5] = {};
  Read9Tag *tag = read9_async(file, 0, 4, buf);
  if (tag == NULL) {
    FAIL("read9_async returned NULL\n");
  }
  int n = read9_wait(tag);
  if (n != 4) {
    FAIL("read9_wait returned %d, expected 4\n", n);
  }
  if (strcmp(buf, "1234") != 0) {
    FAIL("read9_wait, buf is %s, expected 1234\n", buf);
  }
  close9(file);
  unmount9(fsys);
  must_join(server.thrd);
}

static void run_read9_poll_test() {
  Qid9p qids[2] = {};
  TestServer server = {
      .test_name = __func__,
      .script =
          {
              {
                  .type = R_VERSION_9P,
                  .version = {.msize = 1024, .version = "9P2000"},
              },
              {.type = R_ATTACH_9P},
              {
                  .type = R_WALK_9P,
                  .walk =
                      {
                          .nqids = 2,
                          .qids = qids,
                      },
              },
              {
                  .type = R_OPEN_9P,
                  .open = {.iounit = 100},
              },
              {
                  .type = R_READ_9P,
                  .read = {.count = 4, .data = "1234"},
              },
          },
  };
  Fsys9 *fsys = mount9_client(connect_test_server(&server), "test_user");
  File9 *file = open9(fsys, "/foo/bar", OREAD_9);
  if (file == NULL) {
    FAIL("open9 returned NULL\n");
  }
  char buf[5] = {};
  Read9Tag *tag = read9_async(file, 0, 4, buf);
  if (tag == NULL) {
    FAIL("read9_async returned NULL\n");
  }
  Read9PollResult result = {};
  while (!result.done) {
    result = read9_poll(tag);
  }
  if (result.n != 4) {
    FAIL("read9_poll returned %d, expected 4\n", result.n);
  }
  if (strcmp(buf, "1234") != 0) {
    FAIL("read9_poll, buf is %s, expected 1234\n", buf);
  }
  close9(file);
  unmount9(fsys);
  must_join(server.thrd);
}

static void run_write_test() {
  Qid9p qids[2] = {};
  TestServer server = {
      .test_name = __func__,
      .script =
          {
              {
                  .type = R_VERSION_9P,
                  .version = {.msize = 1024, .version = "9P2000"},
              },
              {.type = R_ATTACH_9P},
              {
                  .type = R_WALK_9P,
                  .walk =
                      {
                          .nqids = 2,
                          .qids = qids,
                      },
              },
              {
                  .type = R_OPEN_9P,
                  .open = {.iounit = 100},
              },
              {
                  .type = R_WRITE_9P,
                  .read = {.count = 1},
              },
              {
                  .type = R_WRITE_9P,
                  .read = {.count = 2},
              },
              {
                  .type = R_WRITE_9P,
                  .read = {.count = 1},
              },
          },
  };
  Fsys9 *fsys = mount9_client(connect_test_server(&server), "test_user");
  File9 *file = open9(fsys, "/foo/bar", OREAD_9);
  if (file == NULL) {
    FAIL("open9 returned NULL\n");
  }
  const char *data = "1234";
  int n = write9(file, 4, data);
  if (n != 4) {
    FAIL("write9 returned %d, expected 4\n", n);
  }
  close9(file);
  unmount9(fsys);
  must_join(server.thrd);
}

static void run_write_short_test() {
  Qid9p qids[2] = {};
  TestServer server = {
      .test_name = __func__,
      .script =
          {
              {
                  .type = R_VERSION_9P,
                  .version = {.msize = 1024, .version = "9P2000"},
              },
              {.type = R_ATTACH_9P},
              {
                  .type = R_WALK_9P,
                  .walk =
                      {
                          .nqids = 2,
                          .qids = qids,
                      },
              },
              {
                  .type = R_OPEN_9P,
                  .open = {.iounit = 100},
              },
              {
                  .type = R_WRITE_9P,
                  .read = {.count = 1},
              },
              {
                  .type = R_WRITE_9P,
                  .read = {.count = 0},
              },
              {
                  // Expect a CLUNK after the 0 write.
                  .type = R_CLUNK_9P,
              },
          },
  };
  Fsys9 *fsys = mount9_client(connect_test_server(&server), "test_user");
  File9 *file = open9(fsys, "/foo/bar", OREAD_9);
  if (file == NULL) {
    FAIL("open9 returned NULL\n");
  }
  const char *data = "1234";
  int n = write9(file, 4, data);
  if (n != 1) {
    FAIL("write9 returned %d, expected 1\n", n);
  }
  close9(file);
  unmount9(fsys);
  must_join(server.thrd);
}

static void run_write_error_test() {
  Qid9p qids[2] = {};
  TestServer server = {
      .test_name = __func__,
      .script =
          {
              {
                  .type = R_VERSION_9P,
                  .version = {.msize = 1024, .version = "9P2000"},
              },
              {.type = R_ATTACH_9P},
              {
                  .type = R_WALK_9P,
                  .walk =
                      {
                          .nqids = 2,
                          .qids = qids,
                      },
              },
              {
                  .type = R_OPEN_9P,
                  .open = {.iounit = 100},
              },
              {
                  .type = R_WRITE_9P,
                  .read = {.count = 1},
              },
              {
                  .type = R_WRITE_9P,
                  .read = {.count = 0},
              },
              {
                  .type = R_ERROR_9P,
                  .error = {.message = "test error"},
              },
          },
  };
  Fsys9 *fsys = mount9_client(connect_test_server(&server), "test_user");
  File9 *file = open9(fsys, "/foo/bar", OREAD_9);
  if (file == NULL) {
    FAIL("open9 returned NULL\n");
  }
  const char *data = "1234";
  int n = write9(file, 4, data);
  if (n != 1) {
    FAIL("write9 returned %d, expected 1\n", n);
  }
  close9(file);
  unmount9(fsys);
  must_join(server.thrd);
}

static int server_thread(void *arg) {
  TestServer *server = arg;
  DEBUG("%s SERVER: started\n", server->test_name);
  for (int i = 0; i < sizeof(server->script) / sizeof(server->script[0]) &&
                  server->script[i].type > 0;
       i++) {
    char s[4];
    if (fread(s, 1, sizeof(s), server->socket) != sizeof(s)) {
      if (feof(server->socket)) {
        break;
      }
      FAIL("%s server: failed to read size", server->test_name);
    }
    int size = s[0] | (int)s[1] << 8 | (int)s[2] << 16 | (int)s[3] << 24;
    size -= sizeof(s);
    char *buf = calloc(1, size);
    DEBUG("%s SERVER: reading %d bytes\n", server->test_name, size);
    if (fread(buf, 1, size, server->socket) != size) {
      FAIL("%s server: failed to read message", server->test_name);
    }
    int type = buf[0];
    int tag = buf[1] | (int)buf[2] << 8;
    DEBUG("%s SERVER: read type %d tag %d\n", server->test_name, type, tag);
    free(buf);

    Reply9p *reply = &server->script[i];
    if (reply->type != R_ERROR_9P && reply->type != type + 1) {
      FAIL("%s server: got type %d, expected %d", server->test_name, type,
           reply->type - 1);
    }

    if (reply->internal_data_size == 0) {
      reply = serialize_reply9p(reply, tag);
    }
    if (fwrite((char *)reply + sizeof(Reply9p), 1, reply->internal_data_size,
               server->socket) != reply->internal_data_size) {
      FAIL("%s server: failed to write reply\n", server->test_name);
    }
    DEBUG("%s SERVER: sent message type %d\n", server->test_name, reply->type);
    if (reply != &server->script[i]) {
      free(reply);
    }
  }
  DEBUG("%s SERVER: done\n", server->test_name);
  fclose(server->socket);
  return 0;
}

static Client9p *connect_test_server(TestServer *server) {
  // POSIX puts this in stdio.h, but it is not there with std=c23,
  // we let's just declare it ourselves.
  extern FILE *fdopen(int fd, const char *mode);

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
  if (thrd_create(&server->thrd, server_thread, server) != thrd_success) {
    FAIL("failed to create thread\n");
  }
  server->client = connect_file9p(client);
  return server->client;
}

int main() {
  // Don't SIGPIPE writing to closed socket, return an error.
  signal(SIGPIPE, SIG_IGN);
  run_mount_unmount_test();
  run_mount_version_error_test();
  run_mount_attach_error_test();
  run_open_close_test();
  run_open_walk_error_test();
  run_open_open_error_test();
  run_read_test();
  run_short_read_test();
  run_read_error_test();
  run_read_full_test();
  run_read_full_eof_test();
  run_read_full_unexpected_eof_test();
  run_read_full_error_test();
  run_read9_wait_test();
  run_read9_poll_test();
  run_write_test();
  run_write_short_test();
  run_write_error_test();
  return 0;
}
