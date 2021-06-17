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

#if TESTING_LOADBALANCING

class LoadManager {
  volatile ssize_t         fredCounter;
  WorkerLock               procLock;
  ProcessorStack           waitingProcs;
  FredQueue<FredReadyLink> waitingFreds;

  LoadManagerStats* stats;

  Fred* block(BaseProcessor& proc) {
    procLock.acquire();
    if (waitingFreds.empty()) {
      waitingProcs.push(proc);
      procLock.release();
      return proc.suspend(_friend<LoadManager>());
    } else {
      Fred* nextFred = waitingFreds.pop();
      procLock.release();
      return nextFred;
    }
  }

  void unblock(Fred& fred) {
    procLock.acquire();
    if (waitingProcs.empty()) {
      waitingFreds.push(fred);
      procLock.release();
    } else {
      BaseProcessor* nextProc = waitingProcs.pop();
      procLock.release();
      nextProc->resume(&fred, _friend<LoadManager>());
    }
  }

public:
  LoadManager(cptr_t parent) : fredCounter(0) { stats = new LoadManagerStats(this, parent); }

  bool tryGetReadyFred() {
    ssize_t c = fredCounter;
    return (c > 0) && __atomic_compare_exchange_n(&fredCounter, &c, c-1, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
  }

  Fred* getReadyFred(BaseProcessor& proc) {
    ssize_t fredCount = __atomic_fetch_sub(&fredCounter, 1, __ATOMIC_RELAXED);
    if (fredCount > 0) {
      stats->ready.count(fredCount);     // number of freds ready
      return nullptr;
    } else {
      stats->blocked.count(1-fredCount); // number of procs waiting including this one
      return block(proc);
    }
  }

  bool addReadyFred(Fred& f) {
    if (__atomic_add_fetch(&fredCounter, 1, __ATOMIC_RELAXED) > 0) return false;
    unblock(f);
    return true;
  }
};

#else

class LoadManager {
public:
  LoadManager(cptr_t parent) {}
};

#endif

class Scheduler {
protected:
  WorkerLock     ringLock;
  size_t         ringCount;
  BaseProcessor* placeProc;
  BaseProcessor  stagingProc;

public:
  LoadManager loadManager;
  Scheduler() : ringCount(0), placeProc(nullptr), stagingProc(*this, "Staging    "), loadManager(this) {}
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
