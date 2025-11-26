#ifndef _9FSYS_H_
#define _9FSYS_H_

#include "9p.h"

enum { MAX_OPEN_FILES = 128 };

typedef enum {
  OREAD_9 = 0,
  OWRITE_9 = 1,
  ORDWR_9 = 2,
  OEXEC_9 = 3,

  // Bits ORed to one of the above.
  OTRUNC_9 = 0x10,
  OCEXEC_9 = 0x20,  // close on exec
  ORCLOSE_9 = 0x40, // remove on close
} OpenMode9;

typedef struct fsys9 Fsys9;

typedef struct file9 File9;

// Returns an Fsys9 representing the 9p file system at the Unix socked ns.
//
// Returns NULL and sets errstr on error.
Fsys9 *mount9(const char *ns, const char *user);

// Like mount9, but uses the already connected Client9p.
// The Client9p will be closed by unmount9, and should not be closed by the
// caller.
Fsys9 *mount9_client(Client9p *c, const char *user);

// Releases the resources for the Fsys9.
void unmount9(Fsys9 *fsys);

// Opens a file at the given path from the Fsys9 root
// with the given mode.
//
// Returns NULL and sets errstr on error.
File9 *open9(Fsys9 *fsys, const char *path, OpenMode9 mode);

// Closes the file freeing its resources.
void close9(File9 *file);

// Resets the file position to 0, the beginning of the file.
void rewind9(File9 *file);

// Reads at most count bytes from the file into buf and
// increases the file position by the number of bytes read.
// On error the file position is unchanged.
//
// Returns the number of bytes read or -1 and sets errstr on error; 0 indicates
// end-of-file.
//
// Note: there is an asymmetry between read9 and write9. It is not an error for
// read9 to return fewer bytes than requested (an error is instead indicated
// with a -1 return), but it is an error for write9 to return fewer bytes than
// requested.
int read9(File9 *file, int count, char *buf);

// Reads either 0 bytes (on end-of-file) or exactly count bytes
// from the file into buf and increases the file position
// by the number of bytes read.
//
// The return value is 0 if no bytes were read and end-of-file was reached.
// Otherwise the return value is count, indicating that exactly count bytes were
// read, or it is -1 indicating an error. If end-of-file is reached after
// reading any data, but before reading the full count bytes, -1 is returned. If
// the return value is -1, errstr is set to the error message.
int read9_full(File9 *file, int count, char *buf);

// Reads all of the remaining contents of the file until end-of-file
// and returns it as a \0-terminated string that must be free()d by the caller
// or NULL on error and errstr is set.
char *read9_all(File9 *file);

typedef struct read9_tag Read9Tag;

// Starts a read operation, reading count bytes from the given file offset into
// buf, but does not wait for the data to be read. Instead, it returns a
// Read9Tag* which can be used with read9_poll() to check whether the read is
// complete or read9_wait() to wait for it to complete. The caller is not
// responsible for free()ing the Read9Tag*. Instead if will be freed by a call
// to read9_poll() that indicates the read is complete or by a call to
// read9_wait().
//
// On error, NULL is returned and errstr() is set.
Read9Tag *read9_async(File9 *file, unsigned long offs, int count, char *buf);

// Waits for a read that started with read9_async() to complete, free() the
// memory of the Read9Tag object passed as the argument to the call and returns
// the number of bytes read or -1 and sets errstr on error; 0 indicates
// end-of-file.
//
// If the tag argument is NULL, read9_wait() returns -1.
int read9_wait(Read9Tag *tag);

typedef struct {
  bool done;
  int n;
} Read9PollResult;

// Checks whether a read started with read9_async is complete.
// If the read is complete, the return value has .done==true, and .n contains
// the number of bytes read or -1 and sets errstr on error; 0 indicates
// end-of-file; and the memory of the Read9Tag passed as the argument is
// free()d. If the read is not yet complete, the return value has .done==false,
// .n==0, and the Read9Tag is not free()d, and must be passed to a futher call
// to read9_poll() or read9_wait().
//
// If the tag argument is NULL, read9_po00() returns .done==true, .n==-1.
Read9PollResult read9_poll(Read9Tag *tag);

// Writes count bytes from buf to the file and
// increases the file position by count bytes.
//
// Returns the number of bytes written.
// A return value of less than count indicates an error occurred and errstr will
// be set.
//
// Note: there is an asymmetry between read9 and write9. It is not an error for
// read9 to return fewer bytes than requested (an error is instead indicated
// with a -1 return), but it is an error for write9 to return fewer bytes than
// requested.
int write9(File9 *file, int count, const char *buf);

#endif // _9FSYS_H_
