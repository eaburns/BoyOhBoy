#include "9/acme.h"
#include "9/errstr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
  Acme *acme = acme_connect();
  if (acme == NULL) {
    fprintf(stderr, "acme_connect failed: %s\n", errstr9());
    return 1;
  }
  AcmeWin *win = acme_get_win(acme, "/tmp");
  if (win == NULL) {
    fprintf(stderr, "failed to open win: %s\n", errstr9());
    return 1;
  }
  if (win_fmt_ctl(win, "get\n") < 0) {
    fprintf(stderr, "failed to write to ctl: %s\n", errstr9());
    return 1;
  }
  if (win_fmt_tag(win, "Foo Bar Baz\n") < 0) {
    fprintf(stderr, "failed to write to tag: %s\n", errstr9());
    return 1;
  }
  if (!win_start_events(win)) {
    fprintf(stderr, "failed to start events: %s\n", errstr9());
    return 1;
  }
  for (;;) {
    AcmeEvent *event = win_wait_event(win);
    if (event->type == 0) {
      fprintf(stderr, "error reading event: %s\n", event->data);
      break;
    }
    fprintf(stderr, "got an event\n");
    fprintf(stderr, "	origin: %c\n", event->origin);
    fprintf(stderr, "	type: %c\n", event->type);
    fprintf(stderr, "	addr: %d, %d\n", event->addr[0], event->addr[1]);
    fprintf(stderr, "	flags: %02x\n", event->flags);
    fprintf(stderr, "	count: %d\n", event->count);
    fprintf(stderr, "	data: [%s]\n", event->data);
    if (strchr("xXlLrR", event->type) != NULL) {
      if (!win_write_event(win, event)) {
        fprintf(stderr, "failed to write event to acme: %s\n", errstr9());
        // Errors writing to the event file are generally non-fatal.
      }
    }
    free(event);
  }
  acme_close(acme);
  return 0;
}
