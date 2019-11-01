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
#ifndef _Cluster_h_
#define _Cluster_h_ 1

#include "runtime-glue/RuntimeProcessor.h"

#if TESTING_LOADBALANCING

class Scheduler {
  volatile ssize_t stackCounter;
  RuntimeLock procLock;
  ProcessorList waitingProcs;
  BlockedStackList waitingStacks;

  SchedulerStats* stats;

  StackContext* block(BaseProcessor& proc) {
    procLock.acquire();
    if (waitingStacks.empty()) {
      waitingProcs.push_front(proc);
      procLock.release();
      return reinterpret_cast<RuntimeProcessor&>(proc).suspend();
    } else {
      StackContext* nextStack = waitingStacks.pop_front();
      procLock.release();
      return nextStack;
    }
  }

  void unblock(StackContext& sc) {
    procLock.acquire();
    if (waitingProcs.empty()) {
      waitingStacks.push_back(sc);
      procLock.release();
    } else {
      RuntimeProcessor* anyProc = reinterpret_cast<RuntimeProcessor*>(waitingProcs.pop_front());
      procLock.release();
      anyProc->resume(&sc);
    }
  }

public:
  Scheduler() : stackCounter(0) { stats = new SchedulerStats(this); }

#if TESTING_OPTIMISTIC_ISRS
  void reportReadyStack()  { __atomic_sub_fetch(&stackCounter, 1, __ATOMIC_RELAXED); }
  void correctReadyStack() { __atomic_add_fetch(&stackCounter, 1, __ATOMIC_RELAXED); }
#else
  bool tryGetReadyStack() {
    ssize_t c = stackCounter;
    return (c > 0) && __atomic_compare_exchange_n(&stackCounter, &c, c-1, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
  }
#endif

  StackContext* getReadyStack(BaseProcessor& proc) {
    stats->tasks.count();
    ssize_t blockedCount = - __atomic_sub_fetch(&stackCounter, 1, __ATOMIC_RELAXED);
    if (blockedCount > 0) {
      stats->blocks.count(blockedCount);
      return block(proc);
    }
    return nullptr;
  }

  bool addReadyStack(StackContext& sc) {
    if (__atomic_add_fetch(&stackCounter, 1, __ATOMIC_RELAXED) > 0) return false;
    unblock(sc);
    return true;
  }
};

#else

class Scheduler {};

#endif

class Cluster : public Scheduler {
protected:
  RuntimeClusterLock ringLock;
  size_t             ringCount;
  BaseProcessor*     placeProc;
  BaseProcessor      stagingProc;

public:
  Cluster() : ringCount(0), placeProc(nullptr), stagingProc(*this, "Staging") {}
  ~Cluster() {
    ScopedLock<RuntimeClusterLock> sl(ringLock);
    RASSERT(!ringCount, ringCount);
  }

  void addProcessor(BaseProcessor& proc) {
    ScopedLock<RuntimeClusterLock> sl(ringLock);
    if (placeProc == nullptr) {
      ProcessorRing::close(proc);
      placeProc = &proc;
    } else {
      ProcessorRing::insert_after(*placeProc, proc);
    }
    ringCount += 1;
  }

  void removeProcessor(BaseProcessor& proc) {
    ScopedLock<RuntimeClusterLock> sl(ringLock);
    RASSERT0(placeProc);
    // move placeProc, if necessary
    if (placeProc == &proc) placeProc = ProcessorRing::next(*placeProc);
    // ring empty?
    if (placeProc == &proc) placeProc = nullptr;
    ProcessorRing::remove(proc);
    ringCount -= 1;
  }

  BaseProcessor& placement(_friend<StackContext>, bool sg = false) {
#if TESTING_PLACEMENT_STAGING || TESTING_SHARED_READYQUEUE
    return stagingProc;
#else
#if TESTING_LOADBALANCING
    if (sg) return stagingProc;
#endif
    RASSERT0(placeProc);
    ScopedLock<RuntimeClusterLock> sl(ringLock);
    placeProc = ProcessorRing::next(*placeProc); // ring insert/remove is traversal-safe
    return *placeProc;
#endif
  }

#if TESTING_LOADBALANCING
  StackContext* stage()  {
    return stagingProc.tryDequeue(_friend<Cluster>());
  }
#endif
};

#endif /* _Cluster_h_ */
