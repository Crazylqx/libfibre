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

#include "runtime/Debug.h"
#include "runtime/Stats.h"
#include "runtime/StackContext.h"
#include "runtime-glue/RuntimeLock.h"

class Cluster;
class Scheduler;

class ReadyQueue {
  RuntimeLock readyLock;
#if TESTING_LOCKED_READYQUEUE
  StackQueue<ReadyQueueLink> queue[NumPriority];
#else
  StackMPSC<ReadyQueueLink> queue[NumPriority];
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
    ScopedLock<RuntimeLock> sl(readyLock);
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
    ScopedLock<RuntimeLock> sl(readyLock);
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

protected:
  size_t        stackCount;
  Cluster&      cluster;
  StackContext* idleStack;

  ProcessorStats* stats;

  void idleLoop();

  void yieldDirect(StackContext& sc) {
    StackContext::idleYieldTo(sc, _friend<BaseProcessor>());
  }

  void enqueueDirect(StackContext& s) {
    DBG::outl(DBG::Scheduling, "Stack ", FmtHex(&s), " queueing on ", FmtHex(this));
    stats->enq.count();
    readyQueue.enqueue(s);
  }

public:
  BaseProcessor(Cluster& c, const char* n = "Processor") : stackCount(0), cluster(c), idleStack(nullptr) {
    stats = new ProcessorStats(this, n);
  }

  Cluster& getCluster() { return cluster; }

  void addStack(_friend<StackContext>) {
//    __atomic_add_fetch(&stackCount, 1, __ATOMIC_RELAXED);
  }
  void removeStack(_friend<StackContext>) {
//    __atomic_sub_fetch(&stackCount, 1, __ATOMIC_RELAXED);
  }

#if TESTING_LOADBALANCING
  StackContext* tryDequeue(_friend<Cluster>) {
    return readyQueue.tryDequeue();
  }
#endif

  void enqueueDirect(StackContext& s, _friend<StackContext>) {
    enqueueDirect(s);
  }

  void enqueueResume(StackContext& s, _friend<StackContext>);

  StackContext& scheduleFull(_friend<StackContext>);
  StackContext* scheduleYield(_friend<StackContext>);
  StackContext* scheduleYieldGlobal(_friend<StackContext>);
  StackContext* schedulePreempt(StackContext* currStack,_friend<StackContext>);
};

#endif /* _BaseProcessor_h_ */
