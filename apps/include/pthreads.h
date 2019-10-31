#ifndef _tt_pthreads_h_
#define _tt_pthreads_h_ 1

#include <pthread.h>

class Fibre {
  pthread_t tid;
  typedef void* (*TSR)(void*);
public:
  Fibre(void (*start_routine)(void *), void* arg, bool bg = false) {
    pthread_attr_t attr;
    SYSCALL(pthread_attr_init(&attr));
    SYSCALL(pthread_attr_setstacksize(&attr, 65536));
    SYSCALL(pthread_create(&tid, &attr, (TSR)(void*)start_routine, arg));
    SYSCALL(pthread_attr_destroy(&attr));
  }
  ~Fibre() {
    SYSCALL(pthread_join(tid, nullptr));
  }

  int setaffinity(size_t cpusetsize, const cpu_set_t *cpuset) {
    return pthread_setaffinity_np(tid, cpusetsize, cpuset);
  }

  static void yield() {
#if __FreeBSD__
    pthread_yield();
#else
    SYSCALL(pthread_yield());
#endif
  }
};

class FibreMutex {
  pthread_mutex_t mutex;
  friend class FibreCondition;
public:
  FibreMutex() { SYSCALL(pthread_mutex_init(&mutex, nullptr)); }
  ~FibreMutex() { SYSCALL(pthread_mutex_destroy(&mutex)); }
  void acquire() { SYSCALL(pthread_mutex_lock(&mutex)); }
  bool tryAcquire() { return pthread_mutex_trylock(&mutex) == 0; }
  void release() { SYSCALL(pthread_mutex_unlock(&mutex)); }
};

class FibreCondition {
  pthread_cond_t cond;
public:
  FibreCondition() { SYSCALL(pthread_cond_init(&cond, nullptr)); }
  ~FibreCondition() { SYSCALL(pthread_cond_destroy(&cond)); }

  void wait(FibreMutex& lock) {
    SYSCALL(pthread_cond_wait(&cond, &lock.mutex));
    lock.release();
  }

  template<bool Broadcast = false>
  void signal() {
    if (Broadcast) SYSCALL(pthread_cond_broadcast(&cond));
    else SYSCALL(pthread_cond_signal(&cond));
  }
};

class FibreBarrier {
  pthread_barrier_t barr;
public:
  FibreBarrier(size_t t) { SYSCALL(pthread_barrier_init(&barr, nullptr, t)); }
  ~FibreBarrier() { SYSCALL(pthread_barrier_destroy(&barr)); }

  void wait() {
    int ret = pthread_barrier_wait(&barr);
    if (ret == 0 || ret == PTHREAD_BARRIER_SERIAL_THREAD) return;
    assert(false);
  }
};

#endif /* _tt_pthreads_h_ */
