#ifndef _THREAD_H_
#define _THREAD_H_

#include <pthread.h>

#define ONCE9_INIT PTHREAD_ONCE_INIT

typedef pthread_t Thread9;
typedef pthread_mutex_t Mutex9;
typedef pthread_cond_t Cond9;
typedef pthread_key_t ThreadLocal9;
typedef pthread_once_t Once9;

void thread_create9(Thread9 *thrd, void (*fun)(void *), void *arg);
void thread_join9(Thread9 *thrd);

void do_once9(Once9 *once, void (*fun)());

void thread_local_init9(ThreadLocal9 *local, void (*destroy)(void *));
void *thread_local_get9(ThreadLocal9 *local);
void thread_local_set9(ThreadLocal9 *local, void *val);

void mutex_init9(Mutex9 *mtx);
void mutex_destroy9(Mutex9 *mtx);
void mutex_lock9(Mutex9 *mtx);
void mutex_unlock9(Mutex9 *mtx);

void cond_init9(Cond9 *cnd);
void cond_destroy9(Cond9 *cnd);
void cond_wait9(Cond9 *cnd, Mutex9 *mtx);
void cond_broadcast9(Cond9 *cnd);

#endif // _THREAD_H_