#ifndef _IO_H_
#define _IO_H_

// Dials a Unix socket and returns the file descriptor or -1 and errstr9 is set.
int dial_unix_socket(const char *path);

// Reads size bytes of data from fd into buf and returns the number of bytes
// read or returns -1 and errstr9 is set. If no bytes are read and end-of-file
// is reached, 0 is returned to indicate end-of-file and errstr9 is set to
// "end-of-file" in case the caller would like to treat this as an error. If any
// bytes are read, but not a full size, and end-of-file is reached, it is
// treated as an unexpected end-of-file error.
int read_full(int fd, void *buf, int size);

// Writes size bytes of data from buf into fd and returns the number of bytes
// written or returns -1 and errstr9 is set.
int write_full(int fd, void *buf, int size);

// Just calls close, but the caller needn't import unistd.h.
void close_fd(int fd);

#endif // _IO_H_
