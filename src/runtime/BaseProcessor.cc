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
    if (f->getAffinity()) {
      stats->borrow.count();
    } else {
      stats->stage.count();
      f->changeProcessor(*this, _friend<BaseProcessor>());
    }
  }
  return f;
}

inline Fred* BaseProcessor::trySteal() {
  BaseProcessor* victim = this;
  for (;;) {
#if TESTING_OPTIMISTIC_ISRS
    Fred* l = tryLocal();
    if (l) return l;
#endif
    victim = ProcessorRing::next(*victim);
    if (victim == this) return nullptr;
    Fred* f = victim->readyQueue.tryDequeue();
    if (f) {
      DBG::outl(DBG::Level::Scheduling, "trySteal: ", FmtHex(this), ' ', FmtHex(f));
      stats->steal.count();
      return f;
    }
  }
}

inline Fred* BaseProcessor::scheduleInternal() {
  Fred* nextFred;
  if ((nextFred = tryLocal())) return nextFred;
  if ((nextFred = tryStage())) return nextFred;
  if ((nextFred = trySteal())) return nextFred;
  return nullptr;
}

bool BaseProcessor::addReadyFred(Fred& f) {
  return scheduler.addReadyFred(f);
}
#endif

void BaseProcessor::idleLoop() {
  for (;;) {
#if TESTING_LOADBALANCING
    Fred* nextFred = scheduler.getReadyFred(*this);
    if (nextFred) {
      stats->handover.count();
      yieldDirect(*nextFred);
  continue;
    }
#if TESTING_OPTIMISTIC_ISRS
    nextFred = scheduleInternal();
    if (nextFred) {
      yieldDirect(*nextFred);
  continue;
    }
    // might have gotten a token, but not a stack -> correct
    stats->correction.count();
    scheduler.correctReadyFred();
#else /* TESTING_OPTIMISTIC_ISRS */
    for (;;) {
      nextFred = scheduleInternal();
      if (nextFred) break;
      Pause();
    }
    yieldDirect(*nextFred);
#endif
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
#if TESTING_OPTIMISTIC_ISRS
    Fred* nextFred = scheduleInternal();
    if (nextFred) {
      scheduler.reportReadyFred();
      return *nextFred;
    }
#else /* TESTING_OPTIMISTIC_ISRS */
    if (scheduler.tryGetReadyFred()) {
      for (;;) {
        Fred* nextFred = scheduleInternal();
        if (nextFred) return *nextFred;
        Pause();
      }
    }
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
