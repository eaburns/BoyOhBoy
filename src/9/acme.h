#ifndef _ACME_H_
#define _ACME_H_

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
int acme_win_write_ctl(AcmeWin *win, const char *fmt, ...);
int acme_win_write_addr(AcmeWin *win, const char *fmt, ...);
int acme_win_write_data(AcmeWin *win, int size, const char *data);
int acme_win_write_body(AcmeWin *win, int size, const char *data);

// Reads the entire contents of the corresponding file.
// The returned string is \0-terminated and must be free()d by the caller.
// On error NULL is returned and errstr is set.
char *acme_win_read_addr(AcmeWin *win);
char *acme_win_read_data(AcmeWin *win);
char *acme_win_read_body(AcmeWin *win);

#endif // _ACME_H_