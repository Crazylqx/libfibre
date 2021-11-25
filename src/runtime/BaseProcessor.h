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
#include "runtime/HaltSemaphore.h"
#include "runtime/Stats.h"

class BaseProcessor;
class IdleManager;
class Scheduler;

class ReadyQueue {
  WorkerLock readyLock;
  FredReadyQueue queue[Fred::NumPriority];

  FredStats::ReadyQueueStats* stats;

  ReadyQueue(const ReadyQueue&) = delete;            // no copy
  ReadyQueue& operator=(const ReadyQueue&) = delete; // no assignment

  Fred* dequeueInternal() {
    for (size_t p = 0; p < Fred::NumPriority; p += 1) {
      Fred* f = queue[p].pop();
      if (f) return f;
    }
    return nullptr;
  }

public:
  ReadyQueue(BaseProcessor& bp) { stats = new FredStats::ReadyQueueStats(this, &bp); }

  Fred* dequeue() {
#if TESTING_LOADBALANCING
    ScopedLock<WorkerLock> sl(readyLock);
#endif
    Fred* f = dequeueInternal();
    stats->queue.remove((int)(bool)f);
    return f;
  }

#if TESTING_LOADBALANCING
  Fred* tryDequeue() {
    if (!readyLock.tryAcquire()) return nullptr;
    Fred* f = dequeueInternal();
    stats->queue.remove((int)(bool)f);
    readyLock.release();
    return f;
  }
#endif

  void enqueue(Fred& f) {
    RASSERT(f.getPriority() < Fred::NumPriority, f.getPriority());
#if TESTING_LOCKED_READYQUEUE
    ScopedLock<WorkerLock> sl(readyLock);
#endif
    queue[f.getPriority()].push(f);
    stats->queue.add();
  }

  void reset(BaseProcessor& bp, _friend<EventScope>) {
    new (stats) FredStats::ReadyQueueStats(this, &bp);
  }
};

class BaseProcessor;
typedef IntrusiveList<BaseProcessor,0,3> ProcessorList;
typedef IntrusiveRing<BaseProcessor,1,3> ProcessorRingLocal;
typedef IntrusiveRing<BaseProcessor,2,3> ProcessorRingGlobal;

class BaseProcessor : public DoubleLink<BaseProcessor,3> {
  ReadyQueue    readyQueue;

  static const size_t HaltSpinMax = 64;
  static const size_t IdleSpinMax =  1;

  inline Fred*  tryLocal();
#if TESTING_LOADBALANCING
  inline Fred*  tryStage();
  inline Fred*  trySteal();
  inline Fred*  scheduleInternal();
  bool          addReadyFred(Fred& f);
#else
  Benaphore<>   readyCount;
#endif
  HaltSemaphore haltSem;
  Fred*         handoverFred;

  void enqueueFred(Fred& f) {
    DBG::outl(DBG::Level::Scheduling, "Fred ", FmtHex(&f), " queueing on ", FmtHex(this));
    readyQueue.enqueue(f);
  }

protected:
  Scheduler& scheduler;
  Fred*      idleFred;
#if TESTING_WAKE_FRED_WORKER
  bool       halting;
#endif

  FredStats::ProcessorStats* stats;

  void idleLoop(Fred* initFred = nullptr);

public:
  BaseProcessor(Scheduler& c, const char* n = "Processor  ") : readyQueue(*this), haltSem(0), handoverFred(nullptr), scheduler(c), idleFred(nullptr) {
#if TESTING_WAKE_FRED_WORKER
    halting = false;
#endif
    stats = new FredStats::ProcessorStats(this, &c, n);
  }

  void countFredCreated() { stats->create.count(); }
  void countFredStarted() { stats->start.count(); }

  Scheduler& getScheduler() { return scheduler; }

#if TESTING_WAKE_FRED_WORKER
  bool isHalting(_friend<IdleManager>) { return halting; }
  void setHalting(bool h, _friend<IdleManager>) { halting = h; }
#else
  bool isHalting(_friend<IdleManager>) { return false; }
  void setHalting(bool, _friend<IdleManager>) {}
#endif

#if TESTING_LOADBALANCING
  Fred* tryDequeue(_friend<Scheduler>) {
    return readyQueue.tryDequeue();
  }
#endif

  void enqueueYield(Fred& f, _friend<Fred>) {
    enqueueFred(f);
  }

  void enqueueResume(Fred& f, _friend<Fred>) {
#if TESTING_LOADBALANCING
    if (!addReadyFred(f)) enqueueFred(f);
#else
    enqueueFred(f);
    if (!readyCount.V()) haltSem.V(*this);
#endif
  }

  Fred& scheduleFull(_friend<Fred>);
  Fred* scheduleYield(_friend<Fred>);
  Fred* scheduleYieldGlobal(_friend<Fred>);
  Fred* schedulePreempt(Fred* currFred, _friend<Fred>);

  Fred* suspend(_friend<IdleManager>) {
    for (size_t i = 0; i < HaltSpinMax; i += 1) {
      if fastpath(haltSem.tryP(*this)) return handoverFred;
      Pause();
    }
    stats->idle.count();
    haltSem.P(*this);
    return handoverFred;
  }

  void resume(Fred* f, _friend<IdleManager>) {
    stats->wake.count();
    handoverFred = f;
    haltSem.V(*this);
  }

  void reset(Scheduler& c, _friend<EventScope> token, const char* n = "Processor  ") {
    new (stats) FredStats::ProcessorStats(this, &c, n);
    readyQueue.reset(*this, token);
  }
};

#endif /* _BaseProcessor_h_ */
