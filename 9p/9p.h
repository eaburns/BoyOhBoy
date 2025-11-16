#ifndef _9P_H_
#define _9P_H_

#include <stdint.h>

#define VERSION_9P "9P2000"

typedef struct Client9p Client9p;

typedef int16_t Tag9p;

typedef struct {
  uint8_t bytes[13];
} Qid9p;

typedef enum {
  R_VERSION_9P = 101,
  R_AUTH_9P = 103,
  R_ATTACH_9P = 105,
  R_ERROR_9P = 107,
  R_FLUSH_9P = 109,
} ReplyType9p;

typedef struct {
  uint32_t msize;
  const char *version;
} Rversion9p;

typedef struct {
  const char *message;
} Rerror9p;

typedef struct {
  Qid9p qid;
} Rauth9p;

typedef struct {
  ReplyType9p type;
  union {
    Rversion9p version;
    Rerror9p error;
    Rauth9p auth;
  };
  char internal_data[];
} Reply9p;

Client9p *connect9p(const char *path);
void close9p(Client9p *c);
int max_write_size9p(Client9p *c);
Tag9p version9p(Client9p *c, uint32_t msize, const char *version);
Tag9p auth9p(Client9p *c, const char *uname, const char *aname);

// Caller must free() Reply9p.
// Reply is either the reply, error, or flush.
Reply9p *wait9p(Client9p *c, Tag9p tag);
Reply9p *poll9p(Client9p *c, Tag9p tag); // NULL if not ready

#endif // _9P_H_
