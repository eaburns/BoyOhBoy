#include <stdlib.h>
#include <threads.h>

void must_lock(mtx_t *mtx) {
  if (mtx_lock(mtx) != thrd_success) {
    abort();
  }
}

void must_unlock(mtx_t *mtx) {
  if (mtx_unlock(mtx) != thrd_success) {
    abort();
  }
}

void must_wait(cnd_t *cnd, mtx_t *mtx) {
  if (cnd_wait(cnd, mtx) != thrd_success) {
    abort();
  }
}

void must_broadcast(cnd_t *cnd) {
  if (cnd_broadcast(cnd) != thrd_success) {
    abort();
  }
}