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
#ifndef _BaseProcessor_h_
#define _BaseProcessor_h_ 1

#include "runtime/Benaphore.h"
#include "runtime/Debug.h"
#include "runtime/Fred.h"
#include "runtime/Stats.h"
#include "runtime-glue/RuntimeLock.h"

class LoadManager;
class Scheduler;

class ReadyQueue {
  WorkerLock readyLock;
#if TESTING_LOCKED_READYQUEUE
  FlexFredQueue     queue[NumPriority];
#else
  FlexFredQueueMPSC queue[NumPriority];
#endif

  ReadyQueue(const ReadyQueue&) = delete;            // no copy
  ReadyQueue& operator=(const ReadyQueue&) = delete; // no assignment

  Fred* dequeueInternal() {
#if TESTING_LOCKED_READYQUEUE
    for (size_t p = 0; p < NumPriority; p += 1) {
      if (!queue[p].empty()) return queue[p].pop();
    }
#else
    for (size_t p = 0; p < NumPriority; p += 1) {
      Fred* f = queue[p].pop();
      if (f) return f;
    }
#endif
    return nullptr;
  }

public:
  ReadyQueue() = default;

  Fred* dequeue() {
    ScopedLock<WorkerLock> sl(readyLock);
    return dequeueInternal();
  }

#if TESTING_LOADBALANCING
  Fred* tryDequeue() {
    if (!readyLock.tryAcquire()) return nullptr;
    Fred* f = dequeueInternal();
    readyLock.release();
    return f;
  }
#endif

  void enqueue(Fred& f) {
    RASSERT(f.getPriority() < NumPriority, f.getPriority());
#if TESTING_LOCKED_READYQUEUE
    ScopedLock<WorkerLock> sl(readyLock);
#endif
    queue[f.getPriority()].push(f);
  }
};

class BaseProcessor;
typedef IntrusiveList<BaseProcessor,0,2> ProcessorList;
typedef IntrusiveRing<BaseProcessor,1,2> ProcessorRing;

class BaseProcessor : public DoubleLink<BaseProcessor,2> {
  inline Fred* tryLocal();
#if TESTING_LOADBALANCING
  inline Fred* tryStage();
  inline Fred* trySteal();
  inline Fred* scheduleInternal();
#else
  Benaphore<OsSemaphore> readyCount;
#endif
  ReadyQueue readyQueue;

  void idleLoopTerminate();

  void enqueueDirect(Fred& f) {
    DBG::outl(DBG::Level::Scheduling, "Fred ", FmtHex(&f), " queueing on ", FmtHex(this));
    stats->enq.count();
    readyQueue.enqueue(f);
  }

#if TESTING_LOADBALANCING
  bool addReadyFred(Fred& f);
#endif

protected:
  Scheduler& scheduler;
  Fred*      idleFred;

  WorkerSemaphore haltNotify;
  Fred*           handoverFred;

  ProcessorStats* stats;

  void idleLoop();

  static void yieldDirect(Fred& f) {
    Fred::idleYieldTo(f, _friend<BaseProcessor>());
  }

public:
  BaseProcessor(Scheduler& c, const char* n = "Processor") : scheduler(c), idleFred(nullptr), haltNotify(0), handoverFred(nullptr) {
    stats = new ProcessorStats(this, n);
  }

  Scheduler& getScheduler() { return scheduler; }

#if TESTING_LOADBALANCING
  Fred* tryDequeue(_friend<Scheduler>) {
    return readyQueue.tryDequeue();
  }
#endif

  void enqueueDirect(Fred& f, _friend<Fred>) {
    enqueueDirect(f);
  }

  void enqueueResume(Fred& f, _friend<Fred>) {
#if TESTING_LOADBALANCING
    if (!addReadyFred(f)) enqueueDirect(f);
#else
    enqueueDirect(f);
    readyCount.V();
#endif
  }

  Fred& scheduleFull(_friend<Fred>);
  Fred* scheduleYield(_friend<Fred>);
  Fred* scheduleYieldGlobal(_friend<Fred>);
  Fred* schedulePreempt(Fred* currFred, _friend<Fred>);

  Fred* suspend(_friend<LoadManager>) {
#if TESTING_HALT_SPIN
    static const size_t SpinMax = TESTING_HALT_SPIN;
    for (size_t i = 0; i < SpinMax; i += 1) {
      if fastpath(haltNotify.tryP()) return handoverFred;
      Pause();
    }
#endif
    stats->idle.count();
    haltNotify.P();
    return handoverFred;
  }

  void resume(_friend<LoadManager>, Fred* f = nullptr) {
    stats->wake.count();
    handoverFred = f;
    haltNotify.V();
  }
};

#endif /* _BaseProcessor_h_ */
