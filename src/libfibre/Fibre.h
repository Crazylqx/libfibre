/******************************************************************************
    Copyright (C) Martin Karsten 2015-2021

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
#ifndef _Fibre_h_
#define _Fibre_h_ 1

/** @file */

#include "runtime/BaseProcessor.h"
#include "runtime/BlockingSync.h"
#include "runtime-glue/RuntimeContext.h"

#include <vector>
#include <sys/mman.h> // mmap, munmap, mprotect

#ifdef SPLIT_STACK
extern "C" void __splitstack_block_signals(int* next, int* prev);
extern "C" void __splitstack_block_signals_context(void *context[10], int* next, int* prev);
extern "C" void __splitstack_getcontext(void *context[10]);
extern "C" void __splitstack_setcontext(void *context[10]);
extern "C" void *__splitstack_makecontext(size_t, void *context[10], size_t *);
extern "C" void __splitstack_releasecontext(void *context[10]);
static const size_t defaultStackSize  =  2 * pagesize<1>();
static const size_t defaultStackGuard =  0 * pagesize<1>();
#else
static const size_t defaultStackSize  = 16 * pagesize<1>();
static const size_t defaultStackGuard =  1 * pagesize<1>();
#endif

#if TESTING_ENABLE_DEBUGGING
extern WorkerLock*      _lfGlobalStackLock;
extern GlobalStackList* _lfGlobalStackList;
#endif

class Cluster;

class FibreSpecific {
public:
  static const size_t FIBRE_KEYS_MAX = bitsize<mword>();
  typedef void (*Destructor)(void*);
private:
  static FastMutex mutex;
  static Bitmap<FIBRE_KEYS_MAX> bmap;
  static std::vector<Destructor> destrVector;
  std::vector<void*> valueVector;
protected:
  FibreSpecific(size_t e = 0) : valueVector(e) {}
  void clearSpecific() {
    size_t start = bmap.find();
    if (start >= FIBRE_KEYS_MAX) return;
    size_t idx = start;
    do {
      if (destrVector[idx]) destrVector[idx](valueVector[idx]);
      idx = bmap.findnext(idx);
    } while (idx > start);
  }
public:
  void setspecific(size_t idx, void *value) {
    RASSERT(idx < FIBRE_KEYS_MAX, idx);
    RASSERT(bmap.test(idx), idx);
    if (idx >= valueVector.size()) {
      if (valueVector.size() == 0) valueVector.resize(1);
      while (idx >= valueVector.size()) valueVector.resize(valueVector.size() * 2);
    }
    valueVector[idx] = value;
  }
  void* getspecific(size_t idx) {
    RASSERT(idx < FIBRE_KEYS_MAX, idx);
    RASSERT(bmap.test(idx), idx);
    RASSERT(idx < valueVector.size(), idx);
    return valueVector[idx];
  }
  static size_t key_create(Destructor d = nullptr) {
    ScopedLock<FastMutex> sl(mutex);
    size_t idx = bmap.find(false);
    RASSERT(idx < FIBRE_KEYS_MAX, idx);
    bmap.set(idx);
    if (idx >= destrVector.size()) {
      if (destrVector.size() == 0) destrVector.resize(1);
      while (idx >= destrVector.size()) destrVector.resize(destrVector.size() * 2);
    }
    destrVector[idx] = d;
    return idx;
  }
  static void key_delete(size_t idx) {
    ScopedLock<FastMutex> sl(mutex);
    RASSERT(idx < FIBRE_KEYS_MAX, idx);
    RASSERT(bmap.test(idx), idx);
    bmap.clr(idx);
    destrVector[idx] = nullptr;
  }
};

/** A Fibre object represents an independent execution context backed by a stack. */
class Fibre : public StackContext, public FibreSpecific {
  FloatingPointFlags fp;       // FP context
  size_t stackSize;            // stack size (including guard)
#ifdef SPLIT_STACK
  void* splitStackContext[10]; // memory for split-stack context
#else
  vaddr stackBottom;           // bottom of allocated memory for stack (including guard)
#endif
  SyncPoint<WorkerLock> done;  // synchronization (join) at destructor

  size_t stackAlloc(size_t size, size_t guard) {
#ifdef SPLIT_STACK
    vaddr stackBottom = (vaddr)__splitstack_makecontext(size, splitStackContext, &size);
    int off = 0; // do not block signals (blocking signals is slow!)
    __splitstack_block_signals_context(splitStackContext, &off, nullptr);
#else
    // check that requested size/guard is a multiple of page size
    RASSERT(aligned(size, pagesize<1>()), size);
    RASSERT(aligned(guard, pagesize<1>()), guard);
    // reserve/map size + protection
    size += guard;
    // add PROT_EXEC here to make stack executable (needed for nested C functions)
    ptr_t ptr = mmap(0, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
    RASSERT0(ptr != MAP_FAILED);
    // set up protection page
    if (guard) SYSCALL(mprotect(ptr, guard, PROT_NONE));
    stackBottom = vaddr(ptr);
#endif
    StackContext::initStackPointer(stackBottom + size);
    return size;
  }

  void stackFree() {
#ifdef SPLIT_STACK
    if (stackSize) __splitstack_releasecontext(splitStackContext);
#else
    if (stackSize) SYSCALL(munmap(ptr_t(stackBottom), stackSize));
#endif
  }

  void initDebug() {
#if TESTING_ENABLE_DEBUGGING
    ScopedLock<WorkerLock> sl(*_lfGlobalStackLock);
    _lfGlobalStackList->push_back(*this);
#endif
  }

  void clearDebug() {
#if TESTING_ENABLE_DEBUGGING
    ScopedLock<WorkerLock> sl(*_lfGlobalStackLock);
    _lfGlobalStackList->remove(*this);
#endif
  }

  Fibre* runInternal(ptr_t func, ptr_t p1, ptr_t p2, ptr_t p3) {
    StackContext::start(func, p1, p2, p3);
    return this;
  }

public:
  struct ExitException {};

  /** Constructor. */
  Fibre(Scheduler& sched = Context::CurrProcessor().getScheduler(), size_t size = defaultStackSize, bool background = false, size_t guard = defaultStackGuard)
  : StackContext(sched, background), stackSize(stackAlloc(size, guard)) { initDebug(); }

  /** Constructor setting affinity to processor. */
  Fibre(BaseProcessor &sp, size_t size = defaultStackSize, size_t guard = defaultStackGuard)
  : StackContext(sp, true), stackSize(stackAlloc(size, guard)) { initDebug(); }

  /** Constructor to immediately start fibre with `func(arg)`. */
  Fibre(funcvoid1_t func, ptr_t arg, bool background = false)
  : Fibre(Context::CurrProcessor().getScheduler(), defaultStackSize, background) { run(func, arg); }

  // constructor for idle loop or main loop (bootstrap) on existing pthread stack
  Fibre(BaseProcessor &sp, _friend<Cluster>)
  : StackContext(sp), stackSize(0) { initDebug(); }

  //  explicit final notification for idle loop or main loop (bootstrap) on pthread stack
  void endDirect(_friend<Cluster>) {
    done.post();
  }

  /** Destructor with synchronization. */
  ~Fibre() { join(); }
  /** Explicit join. Called automatically by destructor. */
  void join() { done.wait(); }
  /** Detach fibre (no waiting for join synchronization). */
  void detach() { done.detach(); }
  /** Exit fibre (with join, if not detached). */
  static void exit() __noreturn;

  // callback from StackContext via Runtime after final context switch
  void destroy(_friend<StackContext>) {
    clearSpecific();
    clearDebug();
    stackFree();
    done.post();
  }

  /** Start fibre. */
  Fibre* run(void (*func)()) {
    return runInternal((ptr_t)func, nullptr, nullptr, nullptr);
  }
  /** Start fibre. */
  template<typename T1>
  Fibre* run(void (*func)(T1*), T1* p1) {
    return runInternal((ptr_t)func, (ptr_t)p1, nullptr, nullptr);
  }
  /** Start fibre. */
  template<typename T1, typename T2>
  Fibre* run(void (*func)(T1*, T2*), T1* p1, T2* p2) {
    return runInternal((ptr_t)func, (ptr_t)p1, (ptr_t)p2, nullptr);
  }
  /** Start fibre. */
  template<typename T1, typename T2, typename T3>
  Fibre* run(void (*func)(T1*, T2*, T3*), T1* p1, T2* p2, T3* p3) {
    return runInternal((ptr_t)func, (ptr_t)p1, (ptr_t)p2, (ptr_t)p3);
  }
  /** Start fibre with pthread-type run function. */
  template<typename T1>
  Fibre* run(void* (*func)(T1*), T1* p1) {
    return runInternal((ptr_t)func, (ptr_t)p1, nullptr, nullptr);
  }

  /** Sleep. */
  static void usleep(uint64_t usecs) {
    sleepStack(Time::fromUS(usecs));
  }

  /** Sleep. */
  static void sleep(uint64_t secs) {
    sleepStack(Time(secs, 0));
  }

  // context switching interface
  void deactivate(Fibre& next, _friend<StackContext>) {
    fp.save();
#if defined(SPLIT_STACK)
    __splitstack_getcontext(splitStackContext);
    __splitstack_setcontext(next.splitStackContext);
#endif
  }
  void activate(_friend<StackContext>) {
    fp.restore();
  }
};

/** @brief Obtain pointer to current Fibre object. */
inline Fibre* CurrFibre() {
  return (Fibre*)Context::CurrStack();
}

#endif /* _Fibre_h_ */
