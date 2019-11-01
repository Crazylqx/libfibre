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
#ifndef _Fibre_h_
#define _Fibre_h_ 1

#include "libfibre/FibreCluster.h"

#include <sys/mman.h>

#ifdef SPLIT_STACK
extern "C" void __splitstack_getcontext(void *context[10]);
extern "C" void __splitstack_setcontext(void *context[10]);
extern "C" void *__splitstack_makecontext(size_t, void *context[10], size_t *);
extern "C" void __splitstack_releasecontext(void *context[10]);
#endif

#if TESTING_ENABLE_DEBUGGING
extern InternalLock*    _lfGlobalStackLock;
extern GlobalStackList* _lfGlobalStackList;
#endif

class Fibre : public StackContext {
  FloatingPointFlags   fp;          // FP context
  size_t               stackSize;   // stack size
#ifdef SPLIT_STACK
  void*                splitStackContext[10];
#else
  vaddr                stackBottom; // bottom of allocated memory for stack
#endif
  SyncPoint<InternalLock> done;     // synchronization (join) at destructor

  size_t stackAlloc(size_t size) {
#ifdef SPLIT_STACK
    vaddr stackBottom = (vaddr)__splitstack_makecontext(size, splitStackContext, &size);
#else
    // check that requested size is a multiple of page size
    RASSERT(aligned(size, stackProtection), size);
    // reserve/map size + protection
    ptr_t ptr = mmap(0, size + stackProtection, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
    RASSERT0(ptr != MAP_FAILED);
    // set up protection page
    SYSCALL(mprotect(ptr, stackProtection, PROT_NONE));
    stackBottom = vaddr(ptr) + stackProtection;
#endif
    StackContext::initStackPointer(stackBottom + size);
    return size;
  }

  void stackFree() {
    if (!stackSize) return;
#ifdef SPLIT_STACK
    __splitstack_releasecontext(splitStackContext);
#else
    SYSCALL(munmap(ptr_t(stackBottom - stackProtection), stackSize + stackProtection));
#endif
  }

  void initDebug() {
#if TESTING_ENABLE_DEBUGGING
    ScopedLock<InternalLock> sl(*_lfGlobalStackLock);
    _lfGlobalStackList->push_back(*this);
#endif
  }

  void clearDebug() {
#if TESTING_ENABLE_DEBUGGING
    ScopedLock<InternalLock> sl(*_lfGlobalStackLock);
    _lfGlobalStackList->remove(*this);
#endif
  }

  Fibre* runInternal(ptr_t func, ptr_t p1, ptr_t p2, ptr_t p3) {
    StackContext::start(func, p1, p2, p3);
    return this;
  }

public:
  // general constructor
  Fibre(FibreCluster& cluster = CurrCluster(), size_t sz = defaultStackSize, bool bg = false)
  : StackContext(cluster, bg), stackSize(stackAlloc(sz)) { initDebug(); }

  // constructor with affinity to processor
  Fibre(BaseProcessor &sp, size_t sz = defaultStackSize)
  : StackContext(sp, true), stackSize(stackAlloc(sz)) { initDebug(); }

  // constructor to start fibre right away
  Fibre(funcvoid1_t func, ptr_t p1, bool bg = false)
  : Fibre(CurrCluster(), defaultStackSize, bg) { run(func, p1); }

  // constructor for idle loop or main loop (bootstrap) on existing pthread stack
  Fibre(OsProcessor &sp, _friend<OsProcessor>)
  : StackContext(sp), stackSize(0) { initDebug(); }

  //  explicit final notifcation for idle loop or main loop (bootstrap) on pthread stack
  void endDirect(_friend<OsProcessor>) {
    done.post();
  }

  // synchronize at object destruction
  ~Fibre() { join(); }
  void join() { done.wait(); }
  void detach() { done.detach(); }

  // callback from StackContext via Runtime after final context switch
  void destroy(_friend<StackContext>) {
    clearDebug();
    stackFree();
    done.post();
  }

  // fibre start routines
  Fibre* run(funcvoid0_t func) {
    return runInternal((ptr_t)func, nullptr, nullptr, nullptr);
  }
  template<typename T1>
  Fibre* run(void (*func)(T1*), T1* p1) {
    return runInternal((ptr_t)func, (ptr_t)p1, nullptr, nullptr);
  }
  template<typename T1, typename T2>
  Fibre* run(void (*func)(T1*, T2*), T1* p1, T2* p2) {
    return runInternal((ptr_t)func, (ptr_t)p1, (ptr_t)p2, nullptr);
  }
  template<typename T1, typename T2, typename T3>
  Fibre* run(void (*func)(T1*, T2*, T3*), T1* p1, T2* p2, T3* p3) {
    return runInternal((ptr_t)func, (ptr_t)p1, (ptr_t)p2, (ptr_t)p3);
  }

  template<typename T1>
  Fibre* run(void* (*func)(T1*), T1* p1) {
    return runInternal((ptr_t)func, (ptr_t)p1, nullptr, nullptr);
  }

  // sleep interface
  static void usleep(uint64_t usecs) {
    sleepStack(Time::fromUS(usecs));
  }

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

static inline Fibre* CurrFibre() {
  return (Fibre*)CurrStack();
}

#endif /* _Fibre_h_ */
