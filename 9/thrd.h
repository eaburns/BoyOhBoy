#ifndef _THRD_H_
#define _THRD_H_

#include <threads.h>

// mtx_lock, mtx_unlock, cnd_wait, and cnd_broadcast, but they abort() if the
// return is not thrd_success. There's really not much else we can do, but might
// as well avoid hitting undefined behavior and just abort.
void must_lock(mtx_t *mtx);
void must_unlock(mtx_t *mtx);
void must_wait(cnd_t *cnd, mtx_t *mtx);
void must_broadcast(cnd_t *cnd);

#endif // _THRD_H_