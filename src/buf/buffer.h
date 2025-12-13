#ifndef _BUF_H_
#define _BUF_H_

// A new buffer must be 0-filled.
// So typically Buffer b = {}.
// The data field must be free()d by the caller.
typedef struct {
  char *data;
  int size, cap;
} Buffer;

// Prints a printf-style formatted string to the buffer.
// This allocates/reallocates the Buffer's .data field,
// which must be free()d by the caller.
void bprintf(Buffer *b, const char *fmt, ...);

#endif // _BUF_H_
