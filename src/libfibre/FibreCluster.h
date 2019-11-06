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
#ifndef _FibreCluster_h_
#define _FibreCluster_h_ 1

#include "runtime/Cluster.h"
#include "libfibre/Poller.h"

class FibreCluster : public Cluster {
  EventScope&   scope;

  ClusterPoller* pollVec;
  size_t         pollCount;

  FibreSemaphore pauseSem;
  FibreSemaphore confirmSem;
  FibreSemaphore continueSem;
  OsSemaphore    sleepSem;
  OsProcessor*   pauseProc;

  FibreCluster(EventScope& es, _friend<FibreCluster>, size_t p = 1) : scope(es),
    pollCount(p), sleepSem(1), pauseProc(nullptr) {
      pollVec = (ClusterPoller*)new char[sizeof(ClusterPoller[pollCount])];
      for (size_t p = 0; p < pollCount; p += 1) new (&pollVec[p]) ClusterPoller(es, stagingProc);
    }
public:
  FibreCluster(EventScope& es, size_t p = 1) : FibreCluster(es, _friend<FibreCluster>(), p)
    { for (size_t p = 0; p < pollCount; p += 1) pollVec[p].start(); }
  FibreCluster(size_t p = 1) : FibreCluster(CurrEventScope(), p) {}

  // special constructor and start routine for bootstrapping event scope
  FibreCluster(EventScope& es, _friend<EventScope>, size_t p = 1) : FibreCluster(es, _friend<FibreCluster>(), p) {}
  void startPoller(_friend<EventScope>) { for (size_t p = 0; p < pollCount; p += 1) pollVec[p].start(); }

  EventScope& getEventScope() { return scope; }
  ClusterPoller& getPoller(size_t hint) { return pollVec[hint % pollCount]; }
  size_t getPollerCount() { return pollCount; }

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

  static void maintenance(FibreCluster* fc) {
    for (;;) {
      fc->pauseSem.P();
      fc->confirmSem.V();
      if (fc->pauseProc != &CurrProcessor()) {
        fc->sleepSem.P();
        fc->sleepSem.V();
      }
    }
  }
};

#endif /* _FibreCluster_h_ */
