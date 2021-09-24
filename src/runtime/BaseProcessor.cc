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
  BaseProcessor* victim = this;
  bool local = true;
  for (;;) {
    if (local) {
      victim = ProcessorRingLocal::next(*victim);
      if (victim == this) { local = false; continue; }
    } else {
      victim = ProcessorRingGlobal::next(*victim);
      if (victim == this) return nullptr;
    }
    Fred* f = victim->readyQueue.tryDequeue();
    if (f) {
      DBG::outl(DBG::Level::Scheduling, "trySteal: ", FmtHex(this), ' ', FmtHex(f));
      if (f->checkAffinity(*this, _friend<BaseProcessor>())) {
        if (local) stats->borrowLocal.count(); else stats->borrowGlobal.count();
      } else {
        if (local) stats->stealLocal.count(); else stats->stealGlobal.count();
      }
      return f;
    }
  }
}

inline Fred* BaseProcessor::scheduleInternal() {
  Fred* nextFred;
  do {
    if ((nextFred = tryLocal())) return nextFred;
  } while (RuntimeWorkerPoll(*this));
  if ((nextFred = tryStage())) return nextFred;
  if ((nextFred = trySteal())) return nextFred;
  return nullptr;
}

bool BaseProcessor::addReadyFred(Fred& f) {
#if TESTING_IDLE_MANAGER
  return scheduler.idleManager.addReadyFred(f);
#else
  return false;
#endif
  (void)f;
}
#endif

void BaseProcessor::idleLoop() {
  for (;;) {
#if TESTING_LOADBALANCING
#if TESTING_IDLE_MANAGER
    Fred* nextFred = scheduler.idleManager.getReadyFred(*this);
#else
    Fred* nextFred = nullptr;
#endif
    if (nextFred) {
      DBG::outl(DBG::Level::Scheduling, "handover: ", FmtHex(this), ' ', FmtHex(nextFred));
      nextFred->checkAffinity(*this, _friend<BaseProcessor>());
      stats->handover.count();
    } else {
      do nextFred = scheduleInternal(); while (!nextFred);
    }
    yieldDirect(*nextFred);
#else /* TESTING_LOADBALANCING */
    if (!readyCount.P()) readySem.P();
    Fred* nextFred = tryLocal();
    RASSERT0(nextFred);
    yieldDirect(*nextFred);
#endif
  }
}

Fred& BaseProcessor::scheduleFull(_friend<Fred>) {
#if TESTING_IDLE_SPIN
  static const size_t SpinMax = TESTING_IDLE_SPIN;
#else
  static const size_t SpinMax = 1;
#endif
  for (size_t i = 0; i < SpinMax; i += 1) {
#if TESTING_LOADBALANCING
#if TESTING_IDLE_MANAGER
    if (scheduler.idleManager.tryGetReadyFred()) {
      for (;;) {
        Fred* nextFred = scheduleInternal();
        if (nextFred) return *nextFred;
      }
    }
#else /* TESTING_IDLE_CONTROL */
    Fred* nextFred = scheduleInternal();
    if (nextFred) return *nextFred;
#endif
#else /* TESTING_LOADBALANCING */
    if (readyCount.tryP()) {
      Fred* nextFred = tryLocal();
      RASSERT0(nextFred);
      return *nextFred;
    }
#endif
  }
  return *idleFred;
}

Fred* BaseProcessor::scheduleYield(_friend<Fred>) {
  return tryLocal();
}

Fred* BaseProcessor::scheduleYieldGlobal(_friend<Fred>) {
#if TESTING_LOADBALANCING
  return scheduleInternal();
#else
  return tryLocal();
#endif
}

Fred* BaseProcessor::schedulePreempt(Fred* currFred, _friend<Fred> fsc) {
  if (currFred == idleFred) return nullptr;
  return scheduleYieldGlobal(fsc);
}
