#ifndef _tt_upp_h_
#define _tt_upp_h_

#ifndef UPP_SEMAPHORE
#define UPP_SEMAPHORE 0
#endif

#include "uBarrier.h"
#include "uRealTime.h"
#include "uSemaphore.h"

typedef uProcessor OsProcessor;
#define CurrProcessor uThisProcessor
typedef uCluster FibreCluster;
#define CurrCluster uThisCluster

_Task Fibre {
  void (*start_routine)(void *);
  void* arg;
  void main() { start_routine(arg); }
public:
  Fibre(void (*start_routine)(void *), void* arg, bool = false) : start_routine(start_routine), arg(arg) {}
  _Nomutex static void yield() { uThisTask().uYieldNoPoll(); }
};

#if UPP_SEMAPHORE
class FibreMutex {
  uSemaphore mutex;
public:
  void acquire() { mutex.P(); }
  bool tryAcquire() { return mutex.TryP(); }
  void release() { mutex.V(); }
};
#else
class FibreMutex {
  uOwnerLock mutex;
public:
  void acquire() { mutex.acquire(); }
  bool tryAcquire() { return mutex.tryacquire(); }
  void release() { mutex.release(); }
};
#endif

class FibreCondition {
  uSemaphore sem;
public:
  FibreCondition() : sem(0) {}
  void wait(FibreMutex& lock) { lock.release(); sem.P(); }
  void signal() { sem.V(); }
};

class FibreBarrier {
  uBarrier barr;
public:
  FibreBarrier(size_t t) : barr(t) {}
  void wait() { barr.block(); }
};

unsigned int uDefaultPreemption() { // timeslicing not required
    return 0;
} // uDefaultPreemption
unsigned int uDefaultSpin() {       // kernel schedule-spinning off
    return 0;
} // uDefaultPreemption
unsigned int uMainStackSize() {     // reduce, default 500K
    return 60 * 1000;
} // uMainStackSize

#endif /* _tt_upp_h_ */
