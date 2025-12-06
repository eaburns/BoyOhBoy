#include <stdlib.h>
#include <pthread.h>

void must_lock(pthread_mutex_t *mtx) {
  if (pthread_mutex_lock(mtx) != 0) {
    abort();
  }
}

void must_unlock(pthread_mutex_t *mtx) {
  if (pthread_mutex_unlock(mtx) != 0) {
    abort();
  }
}

void must_wait(pthread_cond_t *cnd, pthread_mutex_t *mtx) {
  if (pthread_cond_wait(cnd, mtx) != 0) {
    abort();
  }
}

void must_broadcast(pthread_cond_t *cnd) {
  if (pthread_cond_broadcast(cnd) != 0) {
    abort();
  }
}