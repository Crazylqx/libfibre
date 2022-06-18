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
#include "runtime/Scheduler.h"

inline Fred* BaseProcessor::tryLocal() {
  Fred* f = readyQueue.dequeue();
  if (f) {
    DBG::outl(DBG::Level::Scheduling, "tryLocal: ", FmtHex(this), ' ', FmtHex(f));
    stats->deq.count();
  }
  return f;
}

#if TESTING_LOADBALANCING
inline Fred* BaseProcessor::tryStage() {
  Fred* f = scheduler.stage();
  if (f) {
    DBG::outl(DBG::Level::Scheduling, "tryStage: ", FmtHex(this), ' ', FmtHex(f));
    if (f->checkAffinity(*this, _friend<BaseProcessor>())) {
      stats->borrowStage.count();
    } else {
      stats->stealStage.count();
    }
  }
  return f;
}

inline Fred* BaseProcessor::trySteal() {
  BaseProcessor* victim = localVictim;
  bool local = true;
  for (;;) {
    victim = local ? ProcessorRingLocal::next(*victim) : ProcessorRingGlobal::next(*victim);
    Fred* f = victim->readyQueue.tryDequeue();
    if (f) {
      DBG::outl(DBG::Level::Scheduling, "trySteal: ", FmtHex(this), "<-", FmtHex(victim), ' ', FmtHex(f));
      if (victim == this) {
        stats->deq.count();
      } else {
        if (f->checkAffinity(*this, _friend<BaseProcessor>())) {
          if (local) stats->borrowLocal.count(); else stats->borrowGlobal.count();
        } else {
          if (local) stats->stealLocal.count(); else stats->stealGlobal.count();
        }
      }
      if (local) localVictim = victim; else globalVictim = victim;
      return f;
    }
    if (local) {
      if (victim == localVictim) {
        local = false;
        victim = globalVictim;
      }
    } else {
      if (victim == globalVictim) return nullptr;
    }
  }
}
#endif

inline Fred* BaseProcessor::scheduleInternal() {
  Fred* nextFred;
  if ((nextFred = tryLocal())) return nextFred;
#if TESTING_LOADBALANCING
  if ((nextFred = tryStage())) return nextFred;
  if (RuntimeWorkerPoll(*this)) {
    if ((nextFred = tryLocal())) return nextFred;
  }
  if ((nextFred = trySteal())) return nextFred;
#endif
  return nullptr;
}

inline Fred* BaseProcessor::scheduleNonblocking() {
#if TESTING_LOADBALANCING
#if TESTING_GO_IDLEMANAGER
  return scheduleInternal();
#else /* TESTING_GO_IDLEMANAGER */
  if (scheduler.idleManager.tryGetReadyFred()) {
    Fred* nextFred;
    do { nextFred = scheduleInternal(); } while (!nextFred);
    return nextFred;
  }
#endif
#else /* TESTING_LOADBALANCING */
  if (readyCount.tryP()) {
    Fred* nextFred;
    do { nextFred = scheduleInternal(); } while (!nextFred);
    return nextFred;
  }
#endif
  return nullptr;
}

inline Fred& BaseProcessor::idleLoopSchedule() {
  Fred* nextFred;
#if TESTING_LOADBALANCING && TESTING_GO_IDLEMANAGER
  for (;;) {
    scheduler.idleManager.incSpinning();
    for (size_t i = 1; i < IdleSpinMax; i += 1) {
      nextFred = scheduleNonblocking();
      if (nextFred) {
        scheduler.idleManager.decSpinningAndUnblock();
        return *nextFred;
      }
    }
    scheduler.idleManager.decSpinning();
    nextFred = scheduler.idleManager.block(*this);
    if (nextFred) {
      DBG::outl(DBG::Level::Scheduling, "handover: ", FmtHex(this), ' ', FmtHex(nextFred));
      nextFred->checkAffinity(*this, _friend<BaseProcessor>());
      stats->handover.count();
      return *nextFred;
    }
  }
#else
  for (size_t i = 1; i < IdleSpinMax; i += 1) {
    nextFred = scheduleNonblocking();
    if (nextFred) return *nextFred;
  }
#if TESTING_LOADBALANCING
  nextFred = scheduler.idleManager.getReadyFred(*this);
  if (nextFred) {
    DBG::outl(DBG::Level::Scheduling, "handover: ", FmtHex(this), ' ', FmtHex(nextFred));
    nextFred->checkAffinity(*this, _friend<BaseProcessor>());
    stats->handover.count();
    return *nextFred;
  }
#else /* TESTING_LOADBALANCING */
  if (!readyCount.P()) haltSem.P(*this);
#endif
  do { nextFred = scheduleInternal(); } while (!nextFred);
  return *nextFred;
#endif
}

void BaseProcessor::idleLoop(Fred* initFred) {
  if (initFred) Fred::idleYieldTo(*initFred, _friend<BaseProcessor>());
  for (;;) {
    Fred& nextFred = idleLoopSchedule();
    Fred::idleYieldTo(nextFred, _friend<BaseProcessor>());
  }
}

void BaseProcessor::enqueueResume(Fred& f, _friend<Fred>) {
#if TESTING_LOADBALANCING
  if (!scheduler.idleManager.addReadyFred(f)) enqueueFred(f);
#else
  enqueueFred(f);
  if (!readyCount.V()) haltSem.V(*this);
#endif
}

Fred& BaseProcessor::scheduleFull(_friend<Fred>) {
  Fred* nextFred = scheduleNonblocking();
  if (nextFred) return *nextFred;
  return *idleFred;
}

Fred* BaseProcessor::scheduleYield(_friend<Fred>) {
  return tryLocal();
}

Fred* BaseProcessor::scheduleYieldGlobal(_friend<Fred>) {
  return scheduleInternal();
}

Fred* BaseProcessor::schedulePreempt(Fred* currFred, _friend<Fred> fsc) {
  if (currFred == idleFred) return nullptr;
  return scheduleYieldGlobal(fsc);
}
