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

  enum State { Run, Pause } state;

  FibreSemaphore maintStartSem;
  FibreSemaphore confirmSem;
  FibreSemaphore continueSem;
  OsSemaphore    pauseSem;
  OsProcessor*   maintenanceProc;

  FibreCluster(EventScope& es, _friend<FibreCluster>, size_t p = 1) : scope(es),
    pollCount(p), state(Run), pauseSem(1), maintenanceProc(nullptr) {
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
    state = Pause;
    maintenanceProc = &CurrProcessor();
    pauseSem.P();
    for (size_t p = 0; p < ringCount; p += 1) maintStartSem.V();
    for (size_t p = 0; p < ringCount; p += 1) confirmSem.P();
  }

  void resume() {
    pauseSem.V();
    ringLock.release();
  }

  void wakeAll() {
    ringLock.acquire();
    state = Run;
    maintenanceProc = nullptr;
    for (size_t p = 0; p < ringCount; p += 1) maintStartSem.V();
    for (size_t p = 0; p < ringCount; p += 1) confirmSem.P();
    for (size_t p = 0; p < ringCount; p += 1) continueSem.V();
    ringLock.release();
  }

  static void maintenance(FibreCluster* fc, OsProcessor* proc) {
    for (;;) {
      fc->maintStartSem.P();
      switch (fc->state) {
      case Pause:
        fc->confirmSem.V();
        if (fc->maintenanceProc != &CurrProcessor()) {
          fc->pauseSem.P();
          fc->pauseSem.V();
        }
        break;
      case Run:
        fc->confirmSem.V();
        fc->continueSem.P();
        break;
      default:
        RABORT(fc->state);
      }
    }
  }
};

#endif /* _FibreCluster_h_ */
