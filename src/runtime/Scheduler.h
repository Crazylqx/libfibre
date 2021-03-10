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
#ifndef _Scheduler_h_
#define _Scheduler_h_ 1

#include "runtime/BaseProcessor.h"
#include "runtime-glue/RuntimeLock.h"

#if TESTING_LOADBALANCING

class LoadManager {
  volatile ssize_t fredCounter;
  WorkerLock       procLock;
  ProcessorList    waitingProcs;
  FlexFredList     waitingFreds;

  LoadManagerStats* stats;

  Fred* block(BaseProcessor& proc) {
    procLock.acquire();
    if (waitingFreds.empty()) {
      waitingProcs.push_front(proc);
      procLock.release();
      return proc.suspend(_friend<LoadManager>());
    } else {
      Fred* nextFred = waitingFreds.pop_front();
      procLock.release();
      return nextFred;
    }
  }

  void unblock(Fred& f) {
    procLock.acquire();
    if (waitingProcs.empty()) {
      waitingFreds.push_back(f);
      procLock.release();
    } else {
      BaseProcessor* nextProc = waitingProcs.pop_front();
      procLock.release();
      nextProc->resume(_friend<LoadManager>(), &f);
    }
  }

public:
  LoadManager() : fredCounter(0) { stats = new LoadManagerStats(this); }

#if TESTING_OPTIMISTIC_ISRS
  void reportReadyFred()  { __atomic_sub_fetch(&fredCounter, 1, __ATOMIC_RELAXED); }
  void correctReadyFred() { __atomic_add_fetch(&fredCounter, 1, __ATOMIC_RELAXED); }
#else
  bool tryGetReadyFred() {
    ssize_t c = fredCounter;
    return (c > 0) && __atomic_compare_exchange_n(&fredCounter, &c, c-1, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
  }
#endif

  Fred* getReadyFred(BaseProcessor& proc) {
    stats->tasks.count();
    ssize_t blockedCount = - __atomic_sub_fetch(&fredCounter, 1, __ATOMIC_RELAXED);
    if (blockedCount > 0) {
      stats->blocks.count(blockedCount);
      return block(proc);
    }
    return nullptr;
  }

  bool addReadyFred(Fred& f) {
    if (__atomic_add_fetch(&fredCounter, 1, __ATOMIC_RELAXED) > 0) return false;
    unblock(f);
    return true;
  }
};

#else

class LoadManager {};

#endif

class Scheduler : public LoadManager {
protected:
  WorkerLock     ringLock;
  size_t         ringCount;
  BaseProcessor* placeProc;
  BaseProcessor  stagingProc;

public:
  Scheduler() : ringCount(0), placeProc(nullptr), stagingProc(*this, "Staging") {}
  ~Scheduler() {
    ScopedLock<WorkerLock> sl(ringLock);
    RASSERT(!ringCount, ringCount);
  }

  void addProcessor(BaseProcessor& proc) {
    ScopedLock<WorkerLock> sl(ringLock);
    if (placeProc == nullptr) {
      ProcessorRing::close(proc);
      placeProc = &proc;
    } else {
      ProcessorRing::insert_after(*placeProc, proc);
    }
    ringCount += 1;
  }

  void removeProcessor(BaseProcessor& proc) {
    ScopedLock<WorkerLock> sl(ringLock);
    RASSERT0(placeProc);
    // move placeProc, if necessary
    if (placeProc == &proc) placeProc = ProcessorRing::next(*placeProc);
    // ring empty?
    if (placeProc == &proc) placeProc = nullptr;
    ProcessorRing::remove(proc);
    ringCount -= 1;
  }

  BaseProcessor& placement(_friend<Fred>, bool staging = false) {
#if TESTING_PLACEMENT_STAGING || TESTING_SHARED_READYQUEUE
    return stagingProc;
#else
#if TESTING_LOADBALANCING
    if (staging) return stagingProc;
#endif
    RASSERT0(placeProc);
    // ring insert is traversal-safe, so could use separate 'placeLock' here
    ScopedLock<WorkerLock> sl(ringLock);
    placeProc = ProcessorRing::next(*placeProc);
    return *placeProc;
#endif
  }

#if TESTING_LOADBALANCING
  Fred* stage()  {
    return stagingProc.tryDequeue(_friend<Scheduler>());
  }
#endif
};

#endif /* _Scheduler_h_ */
