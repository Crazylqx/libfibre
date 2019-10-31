#ifndef _tt_qthreads_h_
#define _tt_qthreads_h_ 1

#include "qthread/barrier.h"

class Fibre {
  void (*start_routine)(void* arg);
  void* arg;
  syncvar_t sync;
  static aligned_t qworker(void* f) {
    Fibre* This = (Fibre*)f;
    This->start_routine(This->arg);
    qthread_syncvar_writeF_const(&This->sync, 0);
    return 0;
  }
public:
  Fibre(void (*start_routine)(void *), void* arg, bool = false) : start_routine(start_routine), arg(arg),
    sync(SYNCVAR_STATIC_INITIALIZER) {
    qthread_syncvar_empty(&sync);
    qthread_spawn(qworker, this, 0, nullptr, 0, nullptr, NO_SHEPHERD, 0);
  }
  ~Fibre() {
    uint64_t dummy;
    qthread_syncvar_readFE(&dummy, &sync);
  }
  static void yield() { qthread_yield(); }
};

class FibreMutex {
  aligned_t mutex;
public:
  void acquire() { qthread_lock(&mutex); }
  void release() { qthread_unlock(&mutex); }
};

class FibreBarrier {
  qt_barrier_t* b;
public:
  FibreBarrier(size_t t) { b = qt_barrier_create(t, REGION_BARRIER); }
  ~FibreBarrier() { qt_barrier_destroy(b); }
  void wait() { qt_barrier_enter(b); }
};

#endif /* _tt_qthreads_h_ */
