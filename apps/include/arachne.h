#ifndef _tt_arachne_h_
#define _tt_arachne_h_ 1

#include "Arachne/Arachne.h"

class Fibre {
  Arachne::ThreadId id;
public:
  Fibre(void (*start_routine)(void *), void* arg, bool = false) {
    id = Arachne::createThread(start_routine, arg);
    Arachne::yield();
  }
  ~Fibre() {
    Arachne::join(id);
  }
  static void yield() { Arachne::yield(); }
};

class FibreMutex {
//  Arachne::SpinLock lock;
  Arachne::SleepLock lock;
public:
  void acquire()    { lock.lock(); }
  void tryAcquire() { lock.try_lock(); }
  void release()    { lock.unlock(); }
};

class FibreBarrier {
  Arachne::SpinLock lock;
  size_t target;
  size_t counter;
  Arachne::ConditionVariable queue;
public:
  FibreBarrier(size_t t) : target(t), counter(0) {}
  void wait() {
    lock.lock();
    counter += 1;
    if (counter == target) {
      queue.notifyAll();
      counter = 0;
    } else {
      Arachne::yield();
      queue.wait(lock);
    }
    lock.unlock();
  }
};

#endif /* _tt_arachne_h_ */
