#ifndef _9P_H_
#define _9P_H_

#include <stdint.h>
#include <stdio.h>

#define VERSION_9P "9P2000"

enum {
  NOFID = 0xFFFFFFFF,

  QUEUE_SIZE = 4
};

typedef struct Client9p Client9p;

typedef int16_t Tag9p;

typedef uint32_t Fid9p;

typedef uint8_t Qid9p[13];

typedef enum : uint8_t {
  OREAD_9P = 0,
  OWRITE_9P = 1,
  ORDWR_9P = 2,
  OEXEC_9P = 3,

  // Bits ORed to one of the above.
  OTRUNC_9P = 0x10,
  OCEXEC_9P = 0x20,  // close on exec
  ORCLOSE_9P = 0x40, // remove on close
} OpenMode9p;

typedef enum : uint8_t {
  R_VERSION_9P = 101,
  R_AUTH_9P = 103,
  R_ATTACH_9P = 105,
  R_ERROR_9P = 107,
  R_FLUSH_9P = 109,
  R_WALK_9P = 111,
  R_OPEN_9P = 113,
  R_READ_9P = 117,
} ReplyType9p;

typedef struct {
  uint32_t msize;
  const char *version;
} Rversion9p;

typedef struct {
  const char *message;
} Rerror9p;

typedef struct {
  Qid9p aqid;
} Rauth9p;

typedef struct {
  Qid9p qid;
} Rattach9p;

typedef struct {
  uint16_t nqids;
  Qid9p *qids; // array of nqid Qid9ps.
} Rwalk9p;

typedef struct {
  Qid9p qid;
  uint32_t iounit;
} Ropen9p;

typedef struct {
  uint32_t count;
  // If this Rread9p is in response to a call to read9p(),
  // this will be the same pointer passed to read9p()'s
  // buf argument.
  const char *data;
} Rread9p;

typedef struct {
  ReplyType9p type;
  union {
    Rversion9p version;
    Rerror9p error;
    Rauth9p auth;
    Rattach9p attach;
    Rwalk9p walk;
    Ropen9p open;
    Rread9p read;
  };
  int internal_data_size;
  char internal_data[];
} Reply9p;

Client9p *connect9p(const char *path);
Client9p *connect_file9p(FILE *f);
void close9p(Client9p *c);
Tag9p version9p(Client9p *c, uint32_t msize, const char *version);
Tag9p auth9p(Client9p *c, Fid9p afid, const char *uname, const char *aname);
Tag9p attach9p(Client9p *c, Fid9p fid, Fid9p afid, const char *uname,
               const char *aname);
Tag9p walk9p(Client9p *c, Fid9p fid, Fid9p new_fid, uint16_t nelms, ...);
Tag9p walk_array9p(Client9p *c, Fid9p fid, Fid9p new_fid, uint16_t nelms,
                   const char **elms);
Tag9p open9p(Client9p *c, Fid9p fid, OpenMode9p mode);
Tag9p read9p(Client9p *c, Fid9p fid, uint64_t offs, uint32_t count, char *buf);

// Caller must free() Reply9p.
// Reply is either the reply, error, or flush.
Reply9p *wait9p(Client9p *c, Tag9p tag);
Reply9p *poll9p(Client9p *c, Tag9p tag); // NULL if not ready

// Takes a Reply9p that is not serialized to internal_data and returns one that
// is. The return value must be free()d by the caller. This is not intended for
// common use, but for unit testing.
//
// Regardless of the reply type, all pointers inside the returned Reply9p point
// to the internal data of the Reply9p itself. So, for example, if the original
// Reply9p, r, has pointers to strings or data, these are copied into the
// returned Reply9p and the original memory can be reused without changing the
// return value.
Reply9p *serialize_reply9p(Reply9p *r, Tag9p tag);

#endif // _9P_H_
