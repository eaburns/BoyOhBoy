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
