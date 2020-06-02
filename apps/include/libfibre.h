#ifndef _tt_libfibre_h_
#define _tt_libfibre_h_ 1

#define __LIBFIBRE__
//#define FibreMutex FastMutex
#include "libfibre/fibre.h"

#define HASTRYLOCK 1

#define CurrCluster   Context::CurrCluster

typedef Fibre          shim_thread_t;
typedef FibreMutex     shim_mutex_t;
typedef FibreCondition shim_cond_t;
typedef FibreBarrier   shim_barrier_t;

static inline shim_thread_t* shim_thread_create(void (*start_routine)(void *), void* arg, bool bg = false) {
  return new Fibre(start_routine, arg, bg);
}
static inline void shim_thread_destroy(shim_thread_t* tid) { delete tid; }
static inline void shim_yield() { Fibre::yield(); }

static inline void shim_mutex_init(shim_mutex_t* mtx)    { new (mtx) shim_mutex_t; }
static inline void shim_mutex_destroy(shim_mutex_t* mtx) {}
static inline void shim_mutex_lock(shim_mutex_t* mtx)    { mtx->acquire(); }
static inline bool shim_mutex_trylock(shim_mutex_t* mtx) { return mtx->tryAcquire(); }
static inline void shim_mutex_unlock(shim_mutex_t* mtx)  { mtx->release(); }

static inline void shim_cond_init(shim_cond_t* cond)                    { new (cond) shim_cond_t; }
static inline void shim_cond_destroy(shim_cond_t* cond)                 {}
static inline void shim_cond_wait(shim_cond_t* cond, shim_mutex_t* mtx) { cond->wait(*mtx); }
static inline void shim_cond_signal(shim_cond_t* cond)                  { cond->signal(); }

static inline shim_barrier_t* shim_barrier_create(size_t cnt) { return new FibreBarrier(cnt); }
static inline void shim_barrier_destroy(shim_barrier_t* barr) { delete barr; }
static inline void shim_barrier_wait(shim_barrier_t* barr)    { barr->wait(); }

#endif /* _tt_libfibre_h_ */
