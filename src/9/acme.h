#ifndef _ACME_H_
#define _ACME_H_

#include <stdbool.h>

typedef struct acme Acme;
typedef struct acme_win AcmeWin;

// Connects to Acme and returns a pointer to the connected instance.
//
// On error NULL is returned and errstr is set.
Acme *acme_connect();

// Closes all AcmeWins and then closes the connection to acme and frees
// resources.
void acme_close(Acme *acme);

// Returns a handle to the win, creating it if there is not yet one with the
// given name. The caller must release the AcmeWin when finished with it.
//
// On error NULL is returned and errstr is set.
AcmeWin *acme_get_win(Acme *acme, const char *name);

// Releases the resources of the AcmeWin, leaving the win itself open within the
// editor.
void acme_release_win(AcmeWin *win);

// Writes to the corresponding file, starting at offset 0.
// Returns the number of bytes written or -1 on error and errstr is set.
int acme_win_fmt_ctl(AcmeWin *win, const char *fmt, ...);
int acme_win_fmt_addr(AcmeWin *win, const char *fmt, ...);
int acme_win_fmt_tag(AcmeWin *win, const char *fmt, ...);
int acme_win_write_data(AcmeWin *win, int size, const char *data);
int acme_win_write_body(AcmeWin *win, int size, const char *data);

// Reads the entire contents of the corresponding file.
// The returned string is \0-terminated and must be free()d by the caller.
// On error NULL is returned and errstr is set.
char *acme_win_read_addr(AcmeWin *win);
char *acme_win_read_data(AcmeWin *win);
char *acme_win_read_body(AcmeWin *win);
char *acme_win_read_tag(AcmeWin *win);

typedef struct {
  char origin;
  char type;
  int addr[2];
  int flags;
  int count;
  // If count==0, there is no data. Otherwise data contains count bytes of data
  // plus a terminating \0.
  char data[];
} AcmeEvent;

// Starts reading events and returns true on success or false on failure and
// errstr9 is set to the error message. After acme_win_start_events() is called,
// acme_win_poll_event() and/or acme_win_wait_event() must be called repeatedly
// to read the events until acme_win_stop_events() is called.
bool acme_win_start_events(AcmeWin *win);

// Stops reading events.
void acme_win_stop_events(AcmeWin *win);

// If there is an AcmeEvent immediately available without blocking,
// it is returned. Otherwise NULL is return if there is not an event ready.
//
// The returned AcmeEvent must be free()d by the caller.
// If events have not been started with acme_win_start_event() or if they have
// subsequently been stopped with acme_win_stop_events without then starting
// them again, then NULL is returned.
//
// If an error occurred reading events, an AcmeEvent is returned with all fields
// set to 0 except for count and data. The data is the \0-terminated error
// message, and the count is the size of the message plus the \0-terminator. A
// reliable way to check for this event is to check for .type==0. Note that such
// an error event must still be free()d by the caller.
AcmeEvent *acme_win_poll_event(AcmeWin *win);

// Returns the next AcmeEvent, blocking until one is ready.
//
// This function never returns NULL and the returned AcmeEvent must be free()d
// by the caller.
//
// If an error occurred reading events, an AcmeEvent is returned with all fields
// set to 0 except for count and data. The data is the \0-terminated error
// message, and the count is the size of the message plus the \0-terminator. A
// reliable way to check for this event is to check for .type==0. Note that such
// an error event must still be free()d by the caller.
AcmeEvent *acme_win_wait_event(AcmeWin *win);

// Writes the event to the win's event file.
// When writing an AcmeEvent to the win's event file, the flag, count, and etxt
// are omitted.
//
// Returns true on success and false on error with errstr set.
// It is an error to call acme_win_write_event() when events have not yet been
// started or have been stopped and not subsequently restarted.
//
// Note that Acme only supports writin events with with type x, X, l, L, r, and
// R. Calling this function with any other event type will result in an error.
bool acme_win_write_event(AcmeWin *win, AcmeEvent *event);

#endif // _ACME_H_
