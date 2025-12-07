#include "thread.h"
#include <pthread.h>
#include <stdlib.h>

struct thread_arg {
  void (*fun)(void *);
  void *arg;
};

static void *start_thread(void *voida) {
  struct thread_arg *a = voida;
  void (*fun)(void *) = a->fun;
  void *arg = a->arg;
  free(a);
  fun(arg);
  return NULL;
}

void thread_create9(Thread9 *thrd, void (*fun)(void *), void *arg) {
  struct thread_arg *a = calloc(1, sizeof(*a));
  a->fun = fun;
  a->arg = arg;
  if (pthread_create(thrd, NULL, start_thread, a) != 0) {
    abort();
  }
}

void thread_join9(Thread9 *thrd) {
  if (pthread_join(*thrd, NULL) != 0) {
    abort();
  }
}

void do_once9(Once9 *once, void (*fun)()) {
  if (pthread_once(once, fun) != 0) {
    abort();
  }
}

void thread_local_init9(ThreadLocal9 *local, void (*destroy)(void *)) {
  if (pthread_key_create(local, destroy) != 0) {
    abort();
  }
}

void *thread_local_get9(ThreadLocal9 *local) {
  void *v = pthread_getspecific(*local);
  if (v == NULL) {
    abort();
  }
  return v;
}

void thread_local_set9(ThreadLocal9 *local, void *val) {
  if (pthread_setspecific(*local, val) != 0) {
    abort();
  }
}

void mutex_init9(Mutex9 *mtx) {
  if (pthread_mutex_init(mtx, NULL) != 0) {
    abort();
  }
}

void mutex_destroy9(Mutex9 *mtx) {
  if (pthread_mutex_destroy(mtx) != 0) {
    abort();
  }
}

void mutex_lock9(Mutex9 *mtx) {
  if (pthread_mutex_lock(mtx) != 0) {
    abort();
  }
}

void mutex_unlock9(Mutex9 *mtx) {
  if (pthread_mutex_unlock(mtx) != 0) {
    abort();
  }
}

void cond_init9(Cond9 *cnd) {
  if (pthread_cond_init(cnd, NULL) != 0) {
    abort();
  }
}

void cond_destroy9(Cond9 *cnd) {
  if (pthread_cond_destroy(cnd) != 0) {
    abort();
  }
}

void cond_wait9(Cond9 *cnd, Mutex9 *mtx) {
  if (pthread_cond_wait(cnd, mtx) != 0) {
    abort();
  }
}

void cond_broadcast9(Cond9 *cnd) {
  if (pthread_cond_broadcast(cnd) != 0) {
    abort();
  }
}