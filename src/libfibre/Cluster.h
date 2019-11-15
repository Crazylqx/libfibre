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

#include "runtime/Scheduler.h"
#include "libfibre/Poller.h"

class Cluster : public Scheduler {
  EventScope&    scope;

#if TESTING_CLUSTER_POLLER_FIBRE
  typedef PollerFibre  PollerType;
#else
  typedef PollerThread PollerType;
#endif
  PollerType*    pollVec;
  size_t         pollCount;

  FibreSemaphore pauseSem;
  OsSemaphore    confirmSem;
  OsSemaphore    sleepSem;
  BaseProcessor* pauseProc;

  ClusterStats*  stats;

  void start() {
    for (size_t p = 0; p < pollCount; p += 1) pollVec[p].start();
  }

  Cluster(EventScope& es, size_t p, _friend<Cluster>) : scope(es), pollCount(p), pauseProc(nullptr) {
    stats = new ClusterStats(this);
    pollVec = (PollerType*)new char[sizeof(PollerType[pollCount])];
    for (size_t p = 0; p < pollCount; p += 1) new (&pollVec[p]) PollerType(scope, stagingProc);
  }

public:
  Cluster(EventScope& es, size_t p = 1) : Cluster(es, p, _friend<Cluster>()) { start(); }
  Cluster(size_t p = 1) : Cluster(Context::CurrEventScope(), p) {}

  // special constructor and start routine for bootstrapping event scope
  Cluster(EventScope& es, size_t p, _friend<EventScope>) : Cluster(es, p, _friend<Cluster>()) {}
  void startPolling(_friend<EventScope>) { start(); }

  EventScope& getEventScope() { return scope; }
  PollerType& getPoller(size_t hint) { return pollVec[hint % pollCount]; }
  size_t getPollerCount() { return pollCount; }

  void pause() {
    ringLock.acquire();
    stats->procs.count(ringCount);
    pauseProc = &Context::CurrProcessor();
    for (size_t p = 0; p < ringCount; p += 1) pauseSem.V();
    for (size_t p = 1; p < ringCount; p += 1) confirmSem.P();
  }

  void resume() {
    for (size_t p = 1; p < ringCount; p += 1) sleepSem.V();
    ringLock.release();
  }

  static void maintenance(Cluster* cl);
};

#endif /* _Cluster_h_ */
