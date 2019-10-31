#ifndef _tt_mordor_h_
#define _tt_mordor_h_ 1

#include "mordor/predef.h"
#include "mordor/fiber.h"
#include "mordor/fibersynchronization.h"
#include "mordor/main.h"
#include "mordor/workerpool.h"

using namespace Mordor;

static void mworker(void (*start_routine)(void *), void* arg) {
  start_routine(arg);
}

static WorkerPool* poolScheduler = nullptr;

class Fibre {
  Fiber::ptr f;
public:
  Fibre(void (*start_routine)(void *), void* arg, bool = false) : f(new Fiber(std::bind(mworker, start_routine, arg))) {
    poolScheduler->schedule(f);
  }
  static void yield() { Scheduler::yield(); }
};

class FibreMutex {
  FiberMutex mutex;
public:
  void acquire() { mutex.lock(); }
  void release() { mutex.unlock(); }
};

class FibreBarrier {
  FiberMutex mtx;
  FiberCondition cond;
  size_t target;
  size_t count;
public:
  FibreBarrier(size_t t) : cond(mtx), target(t) {}
  void wait() {
    mtx.lock();
    count += 1;
    if (count < target) cond.wait();
    else cond.broadcast();
    mtx.unlock();
  }
};

#endif /* _tt_mordor_h_ */
