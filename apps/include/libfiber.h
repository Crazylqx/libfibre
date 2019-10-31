#ifndef _tt_libfiber_h_
#define _tt_libfiber_h_ 1

#include "fiber.h"
#include "fiber_mutex.h"
#include "fiber_cond.h"
#include "fiber_barrier.h"
#include "fiber_io.h"
#include "fiber_manager.h"

class Fibre {
  fiber_t* f;
public:
  Fibre(void (*start_routine)(void *), void* arg, bool = false) {
    f = fiber_create(65536, (fiber_run_function_t)(void*)start_routine, arg);
    assert(f);
  }
  ~Fibre() {
    void* dummy;
    fiber_join(f, &dummy);
  }
  static void yield() { fiber_yield(); }
};

class FibreMutex {
  friend class FibreCondition;
  fiber_mutex_t m;
public:
  FibreMutex() { fiber_mutex_init(&m); }
  ~FibreMutex() { fiber_mutex_destroy(&m); }
  void acquire() { fiber_mutex_lock(&m); }
  void release() { fiber_mutex_unlock(&m); }
};

class FibreCondition {
  fiber_cond_t c;
public:
  FibreCondition() { fiber_cond_init(&c); }
  ~FibreCondition() { fiber_cond_destroy(&c); }
  void wait(FibreMutex& lock) { fiber_cond_wait(&c, &lock.m); }
  template<bool Broadcast = false>
  void signal() {
    if (Broadcast) fiber_cond_broadcast(&c);
    else fiber_cond_signal(&c);
  }
};

class FibreBarrier {
  fiber_barrier_t b;
public:
  FibreBarrier(size_t t) { fiber_barrier_init(&b, t); }
  ~FibreBarrier() { fiber_barrier_destroy(&b); }
  void wait() { fiber_barrier_wait(&b); }
};

#endif /* _tt_qthreads_h_ */
