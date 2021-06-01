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
#ifndef _Cluster_h_
#define _Cluster_h_ 1

#include "runtime/Scheduler.h"
#include "libfibre/Fibre.h"
#include "libfibre/Poller.h"

/**
A Cluster object provides a scheduling scope and uses processors (pthreads)
to execute fibres.  It also manages I/O pollers and provides a
stop-the-world pause mechanism.
*/
class Cluster : public Scheduler {
  EventScope& scope;

#if TESTING_CLUSTER_POLLER_FIBRE
  typedef PollerFibre  PollerType;
#else
  typedef PollerThread PollerType;
#endif
  PollerType* iPollVec;
  PollerType* oPollVec;
  size_t      iPollCount;
  size_t      oPollCount;

  BaseProcessor*              pauseProc;
  LockedSemaphore<WorkerLock> pauseSem;
  WorkerSemaphore             confirmSem;
  WorkerSemaphore             sleepSem;

  ClusterStats* stats;

  struct Worker : public BaseProcessor {
    pthread_t sysThreadId;
    Fibre*    maintenanceFibre;
    char*     sigStack; // only used with split stacks
    Worker(Cluster& c) : BaseProcessor(c), maintenanceFibre(nullptr), sigStack(nullptr) {
      c.Scheduler::addProcessor(*this);
    }
    ~Worker();
    void setIdleLoop(Fibre* f)       { BaseProcessor::idleFred = f; }
    void runIdleLoop()               { BaseProcessor::idleLoop(); }
    static void yieldDirect(Fred& f) { BaseProcessor::yieldDirect(f); }
  };

  static void maintenance(Cluster* cl);

  Cluster(EventScope& es, size_t ipcnt, size_t opcnt = 1) : scope(es), iPollCount(ipcnt), oPollCount(opcnt), pauseProc(nullptr) {
    stats = new ClusterStats(this, &es);
    iPollVec = (PollerType*)new char[sizeof(PollerType[iPollCount])];
    oPollVec = (PollerType*)new char[sizeof(PollerType[oPollCount])];
    for (size_t p = 0; p < iPollCount; p += 1) new (&iPollVec[p]) PollerType(scope, stagingProc, this);
    for (size_t p = 0; p < oPollCount; p += 1) new (&oPollVec[p]) PollerType(scope, stagingProc, this);
  }

  void start() {
    for (size_t p = 0; p < iPollCount; p += 1) iPollVec[p].start();
    for (size_t p = 0; p < oPollCount; p += 1) oPollVec[p].start();
  }

  struct Argpack {
    Cluster* cluster;
    Worker* worker;
    Fibre* initFibre;
  };

  inline void  setupWorker(Fibre*, Worker*);
  static void  initDummy(ptr_t);
  static void  fibreHelper(Worker*);
  static void* threadHelper(Argpack*);
  inline void  registerIdleWorker(Worker* worker, Fibre* initFibre);

public:
  /** Constructor: create Cluster in current EventScope. */
  Cluster(size_t pollerCount = 1) : Cluster(Context::CurrEventScope(), pollerCount) { start(); }

  // Dedicated constructor & helper for EventScope creation.
  Cluster(EventScope& es, size_t pollerCount, _friend<EventScope>) : Cluster(es, pollerCount) {}
  void startPolling(_friend<EventScope>) { start(); }

  ~Cluster() {
    // TODO: wait until all work is done, i.e., all regular fibres have left
    // TODO: delete all processors: join pthread via maintenance fibre?
    delete [] iPollVec;
    delete [] oPollVec;
  }

  void preFork(_friend<EventScope>);
  void postFork1(cptr_t parent, _friend<EventScope>);
  void postFork2(_friend<EventScope>);

#if TESTING_WORKER_POLLER
  static BasePoller& getWorkerPoller();
#endif

  // Register curent system thread (pthread) as worker.
  Fibre* registerWorker(_friend<EventScope>);

  /** Create one new worker (pthread) and add to cluster.
      Start `initFunc(initArg)` as dedicated fibre immediately after creation. */
  pthread_t addWorker(funcvoid1_t initFunc = nullptr, ptr_t initArg = nullptr);
  /** Create new workers (pthreads) and add to cluster. */
  void addWorkers(size_t cnt = 1) { for (size_t i = 0; i < cnt; i += 1) addWorker(); }

  /** Obtain system-level ids for workers (pthread_t). */
  size_t getWorkerSysIDs(pthread_t* tid = nullptr, size_t cnt = 0) {
    ScopedLock<WorkerLock> sl(ringLock);
    BaseProcessor* p = placeProc;
    for (size_t i = 0; i < cnt && i < ringCount; i += 1) {
      tid[i] = reinterpret_cast<Worker*>(p)->sysThreadId;
      p = ProcessorRing::next(*p);
    }
    return ringCount;
  }

  /** Get individual access to pollers. */
  PollerType&  getInputPoller(size_t hint) { return iPollVec[hint % iPollCount]; }
  PollerType& getOutputPoller(size_t hint) { return oPollVec[hint % oPollCount]; }

  /** Obtain number of pollers */
  size_t  getInputPollerCount() { return iPollCount; }
  size_t getOutputPollerCount() { return oPollCount; }

  /** Pause all OsProcessors (except caller).. */
  void pause();
  /** Resume all OsProcessors. */
  void resume();
};

#endif /* _Cluster_h_ */
