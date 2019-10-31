#ifndef _tt_boost_h_
#define _tt_boost_h_ 1

#include <boost/version.hpp> 
#include <boost/fiber/all.hpp>

class Fibre {
  boost::fibers::fiber fiber;
public:
  Fibre(void (*start_routine)(void *), void* arg, bool = false) : fiber(start_routine, arg) {}
  ~Fibre() { fiber.join(); }
  static void yield() { boost::this_fiber::yield(); }
};

class FibreMutex {
  boost::fibers::mutex mutex;
public:
  void acquire() { mutex.lock(); }
  void release() { mutex.unlock(); }
};

class FibreBarrier {
  boost::fibers::barrier barr;
public:
  FibreBarrier(size_t t) : barr(t) {}
  void wait() { barr.wait(); }
};

static pthread_barrier_t tbar;
static boost::fibers::barrier* fbar = nullptr;
static pthread_t* btids = nullptr;

static void* bthread(void* cnt) {
  // set up work spin-based stealing scheduler
//  boost::fibers::use_scheduling_algorithm< boost::fibers::algo::work_stealing>((uintptr_t)cnt, false);
  boost::fibers::use_scheduling_algorithm< boost::fibers::algo::shared_work>();
  // wait for all pthreads to arrive
  pthread_barrier_wait(&tbar);
  // suspend until experiment is done
  fbar->wait();
  return nullptr;
}

static void boost_init(uintptr_t cnt) {
  // set up synchronization
  pthread_barrier_init(&tbar, nullptr, cnt);
  fbar = new boost::fibers::barrier(cnt);
  btids = new pthread_t[cnt - 1];
  // start threads
  for (uintptr_t i = 0; i < cnt - 1; i += 1) {
    pthread_create(&btids[i], nullptr, bthread, (void*)cnt);
  }
  // set up spin-based work stealing scheduler after threads are started
//  boost::fibers::use_scheduling_algorithm< boost::fibers::algo::work_stealing>(cnt, false);
  boost::fibers::use_scheduling_algorithm< boost::fibers::algo::shared_work>();
  // synchronize pthread arrival
  pthread_barrier_wait(&tbar);
}

static void boost_finalize(uintptr_t cnt) {
  // synchronize experiment done
  fbar->wait();
  // finish all pthreads
  for (uintptr_t i = 0; i < cnt - 1; i += 1) {
    pthread_join(btids[i], nullptr);
  }
  delete [] btids;
  delete fbar;
  pthread_barrier_destroy(&tbar);
}

#endif /* _tt_boost_h_ */
