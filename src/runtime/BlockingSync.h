/******************************************************************************
    Copyright (C) Martin Karsten 2015-2019

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/
#ifndef _BlockingSync_h_
#define _BlockingSync_h_ 1

#include "runtime/Benaphore.h"
#include "runtime/Debug.h"
#include "runtime/SpinLocks.h"
#include "runtime/Stats.h"
#include "runtime/StackContext.h"
#include "runtime-glue/RuntimeContext.h"
#include "runtime-glue/RuntimeLock.h"
#include "runtime-glue/RuntimeTimer.h"

#include <list>
#include <map>

class BaseTimer;

// DEADLOCK AVOIDANCE RULE: acquire Timer lock while BlockingQueue lock held, but not vice versa!

class TimerQueue {
  WorkerLock lock;
  std::multimap<Time,BaseTimer*> queue;
  TimerStats* stats;

public:
  typedef typename std::multimap<Time,BaseTimer*>::iterator Handle;

  TimerQueue() { stats = new TimerStats(this); }
  bool empty() const { return queue.empty(); }
  void reinit() { new (stats) TimerStats(this); }

  Handle insert(BaseTimer& bt, const Time& relTimeout, const Time& absTimeout) {
    ScopedLock<WorkerLock> al(lock);
    Handle ret = queue.insert( {absTimeout, &bt} ); // set up timeout
    if (ret == queue.begin()) Runtime::Timer::newTimeout(relTimeout);
    return ret; // returns with lock held
  }

  void erase(Handle titer) {
    ScopedLock<WorkerLock> al(lock);
    queue.erase(titer);
  }

  inline bool checkExpiry(const Time& now, Time& newTime);
};

class BaseTimer {
  TimerQueue::Handle thandle;
protected:
  TimerQueue& tQueue;
  void prepareRelative(const Time& timeout) {
    thandle = tQueue.insert(*this, timeout, timeout + Runtime::Timer::now());
  }
  void prepareAbsolute(const Time& timeout, const Time& now) {
    thandle = tQueue.insert(*this, timeout - now, timeout);
  }
public:
  BaseTimer(TimerQueue& tq = Runtime::Timer::CurrTimerQueue()) : tQueue(tq) {}
  void cancel() { tQueue.erase(thandle); }
  virtual bool checkTimer() { return true; }
  virtual void fireTimer() = 0;
};

class BaseSuspender {
protected:
  static void prepareSuspend() {
    RuntimeDisablePreemption();
  }
  static ptr_t doSuspend(StackContext& cs) {
    ptr_t msg = cs.suspend(_friend<BaseSuspender>());
    RuntimeEnablePreemption();
    return msg;
  }
public:
  static ptr_t park(StackContext& stack = *Context::CurrStack()) {
    prepareSuspend();
    return doSuspend(stack);
  }
};

class ResumeInfo {
protected:
  void setupResumeRace(StackContext& cs) {
    cs.setupResumeRace(*this, _friend<ResumeInfo>());
  }
public:
  virtual void cancelTimer() {}
};

class TimeoutInfo : public virtual BaseSuspender, public BaseTimer {
public:
  StackContext& stack;
  TimeoutInfo(StackContext& stack = *Context::CurrStack()) : stack(stack) {}
  void suspendRelative(const Time& timeout) {
    prepareSuspend();
    prepareRelative(timeout);
    doSuspend(stack);
  }
  virtual void fireTimer() {
    DBG::outl(DBG::Level::Blocking, "Stack ", FmtHex(&stack), " timed out");
    stack.resume();
  }
};

template<typename Lock>
class BlockingInfo : public virtual BaseSuspender, public ResumeInfo {
protected:
  Lock& lock;
public:
  BlockingInfo(Lock& l) : lock(l) {}
  void suspend(StackContext& stack = *Context::CurrStack()) {
    prepareSuspend();
    lock.release();
    doSuspend(stack);
  }
  void suspend(FlexStackList& queue, StackContext& stack = *Context::CurrStack()) {
    setupResumeRace(stack);
    prepareSuspend();
    queue.push_back(stack);
    lock.release();
    doSuspend(stack);
  }
};

template<typename Lock>
class TimeoutBlockingInfo : public BlockingInfo<Lock>, public TimeoutInfo {
  using BaseBI = BlockingInfo<Lock>;
  bool timedOut;
public:
  TimeoutBlockingInfo(Lock& l, StackContext& stack = *Context::CurrStack()) : BaseBI(l), TimeoutInfo(stack), timedOut(false) {}
  bool suspendAbsolute(FlexStackList& queue, const Time& timeout, const Time& now) {
    BaseBI::setupResumeRace(stack);
    prepareSuspend();
    queue.push_back(stack);
    BaseTimer::prepareAbsolute(timeout, now);
    BaseBI::lock.release();
    doSuspend(stack);
    return !timedOut;
  }
  virtual bool checkTimer() {
    return stack.raceResume();
  }
  virtual void fireTimer() {
    timedOut = true;
    BaseBI::lock.acquire();
    FlexStackList::remove(stack);
    BaseBI::lock.release();
    TimeoutInfo::fireTimer();
  }
  virtual void cancelTimer() {
    timedOut = false;
    TimeoutInfo::cancel();
  }
};

inline bool TimerQueue::checkExpiry(const Time& now, Time& newTime) {
  bool retcode = false;
  std::list<BaseTimer*> fireList; // defer event locks
  lock.acquire();
  int cnt = 0;
  for (auto it = queue.begin(); it != queue.end(); ) {
    if (it->first > now) {
      retcode = true;
      newTime = it->first - now;
  break;
    }
#if TESTING_ENABLE_STATISTICS
    cnt += 1;
#endif
    BaseTimer* timer = it->second;
    if (timer->checkTimer()) {
      it = queue.erase(it);
      fireList.push_back(timer);
    } else {
      it = next(it);
    }
  }
  stats->events.add(cnt);
  lock.release();
  for (auto f : fireList) f->fireTimer(); // timeout lock released
  return retcode;
}

static inline void sleepStack(const Time& timeout) {
  TimeoutInfo ti;
  DBG::outl(DBG::Level::Blocking, "Stack ", FmtHex(Context::CurrStack()), " sleep ", timeout);
  ti.suspendRelative(timeout);
}

class BlockingQueue {
  FlexStackList queue;

  BlockingQueue(const BlockingQueue&) = delete;            // no copy
  BlockingQueue& operator=(const BlockingQueue&) = delete; // no assignment

public:
  BlockingQueue() = default;
  ~BlockingQueue() { RASSERT0(empty()); }
  bool empty() const { return queue.empty(); }

  template<typename Lock>
  bool block(Lock& lock, bool wait = true) {
    if (wait) {
      BlockingInfo<Lock> bi(lock);
      DBG::outl(DBG::Level::Blocking, "Stack ", FmtHex(Context::CurrStack()), " blocking on ", FmtHex(&queue));
      bi.suspend(queue);
      DBG::outl(DBG::Level::Blocking, "Stack ", FmtHex(Context::CurrStack()), " continuing on ", FmtHex(&queue));
      return true;
    }
    lock.release();
    return false;
  }

  template<typename Lock>
  bool block(Lock& lock, const Time& timeout) {
    Time now = Runtime::Timer::now();
    if (timeout > now) {
      TimeoutBlockingInfo<Lock> tbi(lock);
      DBG::outl(DBG::Level::Blocking, "Stack ", FmtHex(Context::CurrStack()), " blocking on ", FmtHex(&queue), " timeout ", timeout);
      bool ret = tbi.suspendAbsolute(queue, timeout, now);
      DBG::outl(DBG::Level::Blocking, "Stack ", FmtHex(Context::CurrStack()), " continuing on ", FmtHex(&queue));
      return ret;
    }
    lock.release();
    return false;
  }

  template<bool Enqueue = true>
  StackContext* unblock() {       // not concurrency-safe; better hold lock
    for (StackContext* s = queue.front(); s != queue.edge(); s = FlexStackList::next(*s)) {
      ResumeInfo* ri = s->raceResume();
      if (ri) {
        ri->cancelTimer();
        FlexStackList::remove(*s);
        DBG::outl(DBG::Level::Blocking, "Stack ", FmtHex(s), " resume from ", FmtHex(&queue));
        if (Enqueue) s->resume();
        return s;
      }
    }
    return nullptr;
  }

  void reset() {                  // not concurrency-safe; better hold lock
    StackContext* s = queue.front();
    while (s != queue.edge()) {
      ResumeInfo* ri = s->raceResume();
      StackContext* ns = FlexStackList::next(*s);
      if (ri) {
        ri->cancelTimer();
        FlexStackList::remove(*s);
        DBG::outl(DBG::Level::Blocking, "Stack ", FmtHex(s), " clear/resume from ", FmtHex(&queue));
        s->resume();
      }
      s = ns;
    }
  }
};

enum SemaphoreResult : size_t { SemaphoreTimeout = 0, SemaphoreSucess = 1, SemaphoreWasOpen = 2 };

template<typename Lock, bool Binary = false, typename BQ = BlockingQueue>
class LockedSemaphore {
protected:
  Lock lock;
  volatile ssize_t counter;
  BQ bq;

  void unlock() {}

  template<typename Lock1, typename...Args>
  void unlock(Lock1& l, Args&... args) {
    l.release();
    unlock(args...);
  }

  template<typename... Args>
  SemaphoreResult internalP(const Args&... args) {
    // baton passing: counter unchanged, if blocking fails (timeout)
    if (counter < 1) return bq.block(lock, args...) ? SemaphoreSucess : SemaphoreTimeout;
    counter -= 1;
    lock.release();
    return SemaphoreWasOpen;
  }

public:
  explicit LockedSemaphore(ssize_t c = 0) : counter(c) {}
  ~LockedSemaphore() { destroy(); }
  void reinit(ssize_t c = 0) {
    ScopedLock<Lock> al(lock);
    RASSERT0(bq.empty());
    counter = c;
  }
  void destroy() { // baton passing requires serialization at destruction
    ScopedLock<Lock> al(lock);
    RASSERT0(bq.empty());
  }
  ssize_t getValue() const { return counter; }

  template<typename... Args>
  SemaphoreResult P(const Args&... args) { lock.acquire(); return internalP(args...); }
  SemaphoreResult tryP()                 { lock.acquire(); return internalP(false); }

  template<typename... Args>
  SemaphoreResult unlockP(Args&... args) { lock.acquire(); unlock(args...); return internalP(true); }

  void fakeP(size_t c = 1) {
    ScopedLock<Lock> al(lock);
    if (Binary) counter = 0;
    else counter -= c;
  }

  template<bool Enqueue = true, bool TryOnly = false>
  StackContext* V() {
    ScopedLock<Lock> al(lock);
    StackContext* sc = bq.template unblock<Enqueue>();
    if (sc) return sc;
    if (TryOnly) return nullptr;
    if (Binary) counter = 1;
    else counter += 1;
    return nullptr;
  }

  template<bool Enqueue = true>
  StackContext* tryV() { return V<Enqueue,true>(); }

  template<bool Enqueue = true, bool TryOnly = false>
  StackContext* release() { return V<Enqueue,TryOnly>(); }
};

template<typename Lock, bool Fifo, typename BQ = BlockingQueue>
class LockedMutex {
  Lock lock;
  StackContext* owner;
  BQ bq;

protected:
  template<bool OwnerLock, typename... Args>
  bool internalAcquire(const Args&... args) {
    StackContext* cs = Context::CurrStack();
    if (OwnerLock && cs == owner) return true;
    RASSERT(cs != owner, FmtHex(cs), FmtHex(owner));
    for (;;) {
      lock.acquire();
      if (!owner) break;
      if (!bq.block(lock, args...)) return false; // timeout
      if (Fifo) return true; // owner set via baton passing
    }
    owner = cs;
    lock.release();
    return true;
  }

public:
  LockedMutex() : owner(nullptr) {}
  ~LockedMutex() { destroy(); }
  void destroy() { // baton passing requires serialization at destruction
    ScopedLock<Lock> al(lock);
    RASSERT(owner == nullptr, FmtHex(owner));
    RASSERT0(bq.empty());
  }

  template<typename... Args>
  bool acquire(const Args&... args) { return internalAcquire<false>(args...); }
  bool tryAcquire() { return acquire(false); }

  void release() {
    ScopedLock<Lock> al(lock);
    RASSERT(owner == Context::CurrStack(), FmtHex(owner));
    if (Fifo) {
      owner = bq.unblock();
    } else {
      owner = nullptr;
      bq.unblock();
    }
  }
};

// simple blocking barrier
template<typename Lock, typename BQ = BlockingQueue>
class LockedBarrier {
  Lock lock;
  size_t target;
  size_t counter;
  BQ bq;
public:
  explicit LockedBarrier(size_t t = 1) : target(t), counter(0) { RASSERT0(t > 0); }
  ~LockedBarrier() { destroy(); }
  void destroy() {
    ScopedLock<Lock> al(lock);
    RASSERT0(bq.empty())
  }

  bool wait() {
    lock.acquire();
    counter += 1;
    if (counter == target) {
      while (bq.unblock());
      counter = 0;
      lock.release();
      return true;
    } else {
      bq.block(lock);
      return false;
    }
  }
};

// simple blocking RW lock: release alternates; new readers block when writer waits -> no starvation
template<typename Lock, typename BQ = BlockingQueue>
class LockedRWLock {
  Lock lock;
  ssize_t state;                    // -1 writer, 0 open, >0 readers
  BQ bqR;
  BQ bqW;

  template<typename... Args>
  bool internalAR(const Args&... args) {
    lock.acquire();
    if (state < 0 || !bqW.empty()) {
      if (!bqR.block(lock, args...)) return false;
      lock.acquire();
      bqR.unblock();                // waiting readers can barge after writer
    }
    state += 1;
    lock.release();
    return true;
  }

  template<typename... Args>
  bool internalAW(const Args&... args) {
    lock.acquire();
    if (state != 0) {
      if (!bqW.block(lock, args...)) return false;
      lock.acquire();
    }
    state -= 1;
    lock.release();
    return true;
  }

public:
  LockedRWLock() : state(0) {}
  ~LockedRWLock() { destroy(); }
  void destroy() {
    ScopedLock<Lock> al(lock);
    RASSERT(state == 0, state);
    RASSERT0(bqR.empty());
    RASSERT0(bqW.empty());
  }

  template<typename... Args>
  bool acquireRead(const Args&... args) { return internalAR(args...); }
  bool tryAcquireRead() { return acquireRead(false); }

  template<typename... Args>
  bool acquireWrite(const Args&... args) { return internalAW(args...); }
  bool tryAcquireWrite() { return acquireWrite(false); }

  void release() {
    ScopedLock<Lock> al(lock);
    RASSERT0(state != 0);
    if (state > 0) {             // reader leaves; if open -> writer next
      state -= 1;
      if (state > 0) return;
      if (!bqW.unblock()) bqR.unblock();
    } else {                     // writer leaves -> readers next
      RASSERT0(state == -1);
      state += 1;
      if (!bqR.unblock()) bqW.unblock();
    }
  }
};

template<typename Lock = DummyLock>
class LimitedSemaphore0 {
  Lock lock;
  FlexStackQueueMPSC queue;

public:
  explicit LimitedSemaphore0(ssize_t c = 0) { RASSERT(c == 0, c); }
  ~LimitedSemaphore0() { destroy(); }
  void destroy() { RASSERT0(queue.empty()); }

  SemaphoreResult P(bool wait = true) {
    RASSERT0(wait);
    StackContext* cs = Context::CurrStack();
    queue.push(*cs);
    BaseSuspender::park();
    return SemaphoreSucess;
  }

  template<bool Enqueue = true, bool DirectSwitch = false>
  StackContext* V() {
    ScopedLock<Lock> sl(lock);
    StackContext* next;
    for (;;) {
      next = queue.pop();
      if (next) break;
      Pause();
    }
    if (Enqueue) next->resume<DirectSwitch>();
    return next;
  }
};

// NOTE: on queue after P and before V
template<typename Lock = DummyLock>
class LimitedSemaphore1 {
  Lock lock;
  FlexStackQueueNemesis queue;

public:
  explicit LimitedSemaphore1(ssize_t c = 1) { RASSERT(c == 1, c); }
  ~LimitedSemaphore1() { destroy(); }
  void destroy() { RASSERT0(queue.empty()); }

  SemaphoreResult P(bool wait = true) {
    RASSERT0(wait);
    StackContext* cs = Context::CurrStack();
    if (!queue.push(*cs)) return SemaphoreWasOpen;
    BaseSuspender::park();
    return SemaphoreSucess;
  }

  template<bool Enqueue = true, bool DirectSwitch = false>
  StackContext* V() {
    ScopedLock<Lock> sl(lock);
    StackContext* next = nullptr;
    queue.pop(next);
    if (!next) return nullptr;
    if (Enqueue) next->resume<DirectSwitch>();
    return next;
  }
};

class SimpleMutex0 {
  Benaphore<> ben;
  LimitedSemaphore0<> sem;
public:
  SimpleMutex0() : sem(0) {}
  bool acquire()    { return ben.P() || sem.P(); }
  bool tryAcquire() { return ben.tryP(); }
  void release()    { if (!ben.V()) sem.V(); }
};

class SimpleMutex1 {
  LimitedSemaphore1<> sem;
public:
  SimpleMutex1() : sem(1) {}
  bool acquire() { return sem.P(); }
  void release() { sem.V(); }
};

template<typename Semaphore, int SpinStart, int SpinEnd, int SpinCount>
class SpinMutex {
  StackContext* owner;
  Semaphore sem;

  template<typename... Args>
  bool tryOnly(const Args&... args) { return false; }

  template<typename... Args>
  bool tryOnly(bool wait) { return !wait; }

  bool tryLock(StackContext* cs) {
    StackContext* exp = nullptr;
    return __atomic_compare_exchange_n(&owner, &exp, cs, false, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED);
  }

protected:
  template<bool OwnerLock, typename... Args>
  bool internalAcquire(const Args&... args) {
    StackContext* cs = Context::CurrStack();
    if (OwnerLock && cs == owner) return true;
    RASSERT(cs != owner, FmtHex(cs), FmtHex(owner));
    if (tryOnly(args...)) return tryLock(cs);
    int cnt = 0;
    int spin = SpinStart;
    for (;;) {
      if (tryLock(cs)) return true;
      if (cnt < SpinCount) {
        for (int i = 0; i < spin; i += 1) Pause();
        if (spin < SpinEnd) spin += spin;
        else cnt += 1;
      } else {
        cnt = 0;
        spin = SpinStart;
        if (!sem.P(args...)) return false;
      }
    }
  }

public:
  SpinMutex() : owner(nullptr), sem(1) {}
  ~SpinMutex() { RASSERT(owner == nullptr, FmtHex(owner)); }
  void destroy() {
    RASSERT(owner == nullptr, FmtHex(owner));
    sem.destroy();
  }

  template<typename... Args>
  bool acquire(const Args&... args) { return internalAcquire<false>(args...); }
  bool tryAcquire() { return acquire(false); }

  void release() {
    RASSERT(owner == Context::CurrStack(), FmtHex(owner));
    owner = nullptr;
    StackContext* next = sem.template V<false>(); // memory sync via sem.V()
    if (next) next->resume();
  }
};

template<typename BaseMutex>
class OwnerMutex : private BaseMutex {
  size_t counter;
  bool recursion;

public:
  OwnerMutex() : counter(0), recursion(false) {}
  void destroy() { BaseMutex::destroy(); }
  void enableRecursion() { recursion = true; }

  template<typename... Args>
  size_t acquire(const Args&... args) {
    bool success = recursion
      ? BaseMutex::template internalAcquire<true>(args...)
      : BaseMutex::template internalAcquire<false>(args...);
    if (success) return ++counter; else return 0;
  }
  size_t tryAcquire() { return acquire(false); }

  size_t release() {
    if (--counter > 0) return counter;
    BaseMutex::release();
    return 0;
  }
};

// simple blocking condition variable: assume caller holds lock
template<typename BQ = BlockingQueue>
class Condition {
  BQ bq;

public:
  ~Condition() { destroy(); }
  void destroy() { RASSERT0(bq.empty()); }

  template<typename Lock>
  bool wait(Lock& lock) { return bq.block(lock); }

  template<typename Lock>
  bool wait(Lock& lock, const Time& timeout) { return bq.block(lock, timeout); }

  template<bool Broadcast = false>
  void signal() { while (bq.unblock() && Broadcast); }
};

// cf. condition variable: assume caller to wait/post holds lock
template<typename Lock>
class SynchronizedFlag {

public:
  enum State { Running = 0, Dummy = 1, Posted = 2, Detached = 4 };

protected:
  union {                             // 'waiter' set <-> 'state == Waiting'
    StackContext* waiter;
    State         state;
  };

public:
  static const State Invalid = Dummy; // dummy bit never set (for ManagedArray)
  explicit SynchronizedFlag(State s = Running) : state(s) {}
  bool posted()   const { return state == Posted; }
  bool detached() const { return state == Detached; }

  bool wait(Lock& lock) {             // returns false, if detached
    if (state == Running) {
      BlockingInfo<Lock> bi(lock);
      waiter = Context::CurrStack();
      bi.suspend(*waiter);
      lock.acquire();                 // reacquire lock to check state
    }
    if (state == Posted) return true;
    if (state == Detached) return false;
    RABORT(FmtHex(state));
  }

  bool post() {                       // returns false, if already detached
    RASSERT0(state != Posted);       // check for spurious posts
    if (state == Detached) return false;
    if (state != Running) waiter->resume();
    state = Posted;
    return true;
  }

  void detach() {                     // returns false, if already posted or detached
    RASSERT0(state != Detached && state != Posted);
    if (state != Running) waiter->resume();
    state = Detached;
  }
};

// cf. condition variable: assume caller to wait/post holds lock
template<typename Runner, typename Result, typename Lock>
class Joinable : public SynchronizedFlag<Lock> {
  using Baseclass = SynchronizedFlag<Lock>;

  union {
    Runner* runner;
    Result  result;
  };

public:
  Joinable(Runner* t) : runner(t) {}

  bool wait(Lock& bl, Result& r) {
    bool retcode = Baseclass::wait(bl);
    r = retcode ? result : 0; // result is available after returning from wait
    return retcode;
  }

  bool post(Result r) {
    bool retcode = Baseclass::post();
    if (retcode) result = r;  // still holding lock while setting result
    return retcode;
  }

  Runner* getRunner() const { return runner; }
};

template<typename Lock>
class SyncPoint : public SynchronizedFlag<Lock> {
  using Baseclass = SynchronizedFlag<Lock>;
  typedef typename Baseclass::State State;
  Lock lock;
public:
  SyncPoint(State s = Baseclass::Running) : Baseclass(s) {}
  bool wait()   { ScopedLock<Lock> al(lock); return Baseclass::wait(lock); }
  bool post()   { ScopedLock<Lock> al(lock); return Baseclass::post(); }
  void detach() { ScopedLock<Lock> al(lock); Baseclass::detach(); }
};

template<typename Semaphore, bool Binary = false>
class StackContextBenaphore {
  Benaphore<Binary> ben;
  Semaphore sem;

public:
  explicit StackContextBenaphore(ssize_t c) : ben(c), sem(0) {}
  void destroy() { sem.destroy();  }

  SemaphoreResult P()          { return ben.P() ? SemaphoreWasOpen : sem.P(); }
  SemaphoreResult tryP()       { return ben.tryP() ? SemaphoreWasOpen : SemaphoreTimeout; }
  SemaphoreResult P(bool wait) { return wait ? P() : tryP(); }

  template<bool Enqueue = true>
  StackContext* V() {
    if (ben.V()) return nullptr;
    return sem.template V<Enqueue>();
  }
};

template<typename Lock = BinaryLock<>>
class FastBarrier : public BaseSuspender {
  size_t target;
  volatile size_t counter;
  FlexStackQueueMPSC queue;
  Lock lock;

public:
  explicit FastBarrier(size_t t = 1) : target(t), counter(0) { RASSERT0(t > 0); }
  ~FastBarrier() { destroy(); }
  void destroy() { RASSERT0(queue.empty()); }
  bool wait() {
    // There's a race between counter and queue.  A thread can be in a
    // different position in the queue relative to the counter.  The
    // notifier is determined by counter, so a thread might notify a group
    // of other threads, but itself not continue.  However, one thread of
    // each group must be "special" (PTHREAD_BARRIER_SERIAL_THREAD) with a
    // different return code.
    StackContext* cs = Context::CurrStack();
    queue.push(*cs);
    bool park = __atomic_add_fetch(&counter, 1, __ATOMIC_RELAXED) % target;
    if (!park) {
      __atomic_sub_fetch(&counter, target, __ATOMIC_RELAXED);
      park = true;
      ScopedLock<Lock> sl(lock);
      for (size_t i = 0; i < target; i += 1) {
        StackContext* next;
        for (;;) {
          next = queue.pop();
          if (next) break;
          Pause();
        }
        // don't suspend/resume self
        if (next == cs) park = false;
        // if current thread is not continuing, last of group is notified
        else next->resume(ptr_t(park && (i == target - 1)));
      }
    }
    if (park) return (bool)BaseSuspender::park();
    else return true;
  }
};

template<typename Lock>
#if TESTING_MUTEX_FIFO
class Mutex : public LockedMutex<Lock, true> {};
#elif TESTING_MUTEX_BARGING
class Mutex : public LockedMutex<Lock, false> {};
#elif TESTING_MUTEX_SPIN
class Mutex : public SpinMutex<LockedSemaphore<Lock, true>, 4, 1024, 16> {};
#else
class Mutex : public SpinMutex<LockedSemaphore<Lock, true>, 0, 0, 0> {};
#endif

#if TESTING_MUTEX_SPIN
typedef SpinMutex<StackContextBenaphore<LimitedSemaphore0<BinaryLock<>>,true>, 4, 1024, 16> FastMutex;
#else
typedef SpinMutex<StackContextBenaphore<LimitedSemaphore0<BinaryLock<>>,true>, 0, 0, 0> FastMutex;
#endif

#endif /* _BlockingSync_h_ */
