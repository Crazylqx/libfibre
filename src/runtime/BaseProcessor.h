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
#ifndef _BaseProcessor_h_
#define _BaseProcessor_h_ 1

#include "runtime/Benaphore.h"
#include "runtime/Debug.h"
#include "runtime/Stats.h"
#include "runtime/StackContext.h"
#include "runtime-glue/RuntimeLock.h"

class LoadManager;
class Scheduler;

class ReadyQueue {
  WorkerLock readyLock;
#if TESTING_LOCKED_READYQUEUE
  FlexStackQueue queue[NumPriority];
#else
  FlexStackMPSC queue[NumPriority];
#endif

  ReadyQueue(const ReadyQueue&) = delete;            // no copy
  ReadyQueue& operator=(const ReadyQueue&) = delete; // no assignment

  StackContext* dequeueInternal() {
#if TESTING_LOCKED_READYQUEUE
    for (size_t p = 0; p < NumPriority; p += 1) {
      if (!queue[p].empty()) return queue[p].pop();
    }
#else
    for (size_t p = 0; p < NumPriority; p += 1) {
      StackContext* s = queue[p].pop();
      if (s) return s;
    }
#endif
    return nullptr;
  }

public:
  ReadyQueue() = default;

  StackContext* dequeue() {
    ScopedLock<WorkerLock> sl(readyLock);
    return dequeueInternal();
  }

#if TESTING_LOADBALANCING
  StackContext* tryDequeue() {
    if (!readyLock.tryAcquire()) return nullptr;
    StackContext* s = dequeueInternal();
    readyLock.release();
    return s;
  }
#endif

  void enqueue(StackContext& s) {
    RASSERT(s.getPriority() < NumPriority, s.getPriority());
#if TESTING_LOCKED_READYQUEUE
    ScopedLock<WorkerLock> sl(readyLock);
#endif
    queue[s.getPriority()].push(s);
  }
};

class BaseProcessor;
typedef IntrusiveList<BaseProcessor,0,2> ProcessorList;
typedef IntrusiveRing<BaseProcessor,1,2> ProcessorRing;

class BaseProcessor : public ProcessorRing::Link {
  inline StackContext* tryLocal();
#if TESTING_LOADBALANCING
  inline StackContext* tryStage();
  inline StackContext* trySteal();
  inline StackContext* scheduleInternal();
#else
  Benaphore<OsSemaphore> readyCount;
#endif
  ReadyQueue readyQueue;

  void idleLoopTerminate();

  void enqueueDirect(StackContext& s) {
    DBG::outl(DBG::Level::Scheduling, "Stack ", FmtHex(&s), " queueing on ", FmtHex(this));
    stats->enq.count();
    readyQueue.enqueue(s);
  }

#if TESTING_LOADBALANCING
  bool addReadyStack(StackContext& s);
#endif

protected:
  Scheduler&    scheduler;
  StackContext* idleStack;

  WorkerSemaphore haltNotify;
  StackContext*   handoverStack;

  ProcessorStats* stats;

  void idleLoop();

  static void yieldDirect(StackContext& sc) {
    StackContext::idleYieldTo(sc, _friend<BaseProcessor>());
  }

public:
  BaseProcessor(Scheduler& c, const char* n = "Processor") : scheduler(c), idleStack(nullptr), haltNotify(0), handoverStack(nullptr) {
    stats = new ProcessorStats(this, n);
  }

  Scheduler& getScheduler() { return scheduler; }

#if TESTING_LOADBALANCING
  StackContext* tryDequeue(_friend<Scheduler>) {
    return readyQueue.tryDequeue();
  }
#endif

  void enqueueDirect(StackContext& s, _friend<StackContext>) {
    enqueueDirect(s);
  }

  void enqueueResume(StackContext& s, _friend<StackContext>) {
#if TESTING_LOADBALANCING
    if (!addReadyStack(s)) enqueueDirect(s);
#else
    enqueueDirect(s);
    readyCount.V();
#endif
  }

  StackContext& scheduleFull(_friend<StackContext>);
  StackContext* scheduleYield(_friend<StackContext>);
  StackContext* scheduleYieldGlobal(_friend<StackContext>);
  StackContext* schedulePreempt(StackContext* currStack,_friend<StackContext>);

  StackContext* suspend(_friend<LoadManager>) {
#if TESTING_HALT_SPIN
    static const size_t SpinMax = TESTING_HALT_SPIN;
    for (size_t i = 0; i < SpinMax; i += 1) {
      if fastpath(haltNotify.tryP()) return handoverStack;
      Pause();
    }
#endif
    stats->idle.count();
    haltNotify.P();
    return handoverStack;
  }

  void resume(_friend<LoadManager>, StackContext* sc = nullptr) {
    stats->wake.count();
    handoverStack = sc;
    haltNotify.V();
  }
};

#endif /* _BaseProcessor_h_ */
