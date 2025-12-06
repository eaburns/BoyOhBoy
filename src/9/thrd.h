#ifndef _THRD_H_
#define _THRD_H_

#include <pthread.h>

// mtx_lock, mtx_unlock, cnd_wait, and cnd_broadcast, but they abort() if the
// return is not thrd_success. There's really not much else we can do, but might
// as well avoid hitting undefined behavior and just abort.
void must_lock(pthread_mutex_t *mtx);
void must_unlock(pthread_mutex_t *mtx);
void must_wait(pthread_cond_t *cnd, pthread_mutex_t *mtx);
void must_broadcast(pthread_cond_t *cnd);

#endif // _THRD_H_