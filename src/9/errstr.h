#ifndef _ERRSTR_H_
#define _ERRSTR_H_

// Returns the thread-local error string, which is never NULL.
//
// Most functions (refer to their documents) set the thread-local error string
// if they return an error value. It will contain a human-readable error message
// describing the failure.
const char *errstr9();

// Sets the thread-local error string to the printf-style formatted string.
void errstr9f(const char *fmt, ...);

#endif // _ERRSTR_H_
