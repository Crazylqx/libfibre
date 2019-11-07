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
  EventScope&   scope;

  ClusterPoller* pollVec;
  size_t         pollCount;

  FibreSemaphore pauseSem;
  FibreSemaphore confirmSem;
  FibreSemaphore continueSem;
  OsSemaphore    sleepSem;
  OsProcessor*   pauseProc;

  Cluster(EventScope& es, _friend<Cluster>, size_t p = 1) : scope(es),
    pollCount(p), sleepSem(1), pauseProc(nullptr) {
      pollVec = (ClusterPoller*)new char[sizeof(ClusterPoller[pollCount])];
      for (size_t p = 0; p < pollCount; p += 1) new (&pollVec[p]) ClusterPoller(es, stagingProc);
    }
public:
  Cluster(EventScope& es, size_t p = 1) : Cluster(es, _friend<Cluster>(), p)
    { for (size_t p = 0; p < pollCount; p += 1) pollVec[p].start(); }
  Cluster(size_t p = 1) : Cluster(CurrEventScope(), p) {}

  // special constructor and start routine for bootstrapping event scope
  Cluster(EventScope& es, _friend<EventScope>, size_t p = 1) : Cluster(es, _friend<Cluster>(), p) {}
  void startPolling(_friend<EventScope>) { for (size_t p = 0; p < pollCount; p += 1) pollVec[p].start(); }

  EventScope& getEventScope() { return scope; }
  ClusterPoller& getPoller(size_t hint) { return pollVec[hint % pollCount]; }

  void pause() {
    ringLock.acquire();
    pauseProc = &CurrProcessor();
    sleepSem.P();
    for (size_t p = 0; p < ringCount; p += 1) pauseSem.V();
    for (size_t p = 0; p < ringCount; p += 1) confirmSem.P();
  }

  void resume() {
    sleepSem.V();
    ringLock.release();
  }

  static void maintenance(Cluster* cl) {
    for (;;) {
      cl->pauseSem.P();
      cl->confirmSem.V();
      if (cl->pauseProc != &CurrProcessor()) {
        cl->sleepSem.P();
        cl->sleepSem.V();
      }
    }
  }
};

#endif /* _Cluster_h_ */
