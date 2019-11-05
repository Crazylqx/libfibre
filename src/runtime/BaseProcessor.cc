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
#include "runtime/Cluster.h"

inline StackContext* BaseProcessor::tryLocal() {
  StackContext* s = readyQueue.dequeue();
  if (s) {
    DBG::outl(DBG::Level::Scheduling, "tryLocal: ", FmtHex(this), ' ', FmtHex(s));
    stats->deq.count();
  }
  return s;
}

#if TESTING_LOADBALANCING
inline StackContext* BaseProcessor::tryStage() {
  StackContext* s = cluster.stage();
  if (s) {
    DBG::outl(DBG::Level::Scheduling, "tryStage: ", FmtHex(this), ' ', FmtHex(s));
    if (s->getAffinity()) {
      stats->borrow.count();
    } else {
      stats->stage.count();
      s->changeProcessor(*this, _friend<BaseProcessor>());
    }
  }
  return s;
}

inline StackContext* BaseProcessor::trySteal() {
  BaseProcessor* sp = this;
  for (;;) {
#if TESTING_OPTIMISTIC_ISRS
    StackContext* l = tryLocal();
    if (l) return l;
#endif
    sp = ProcessorRing::next(*sp);
    if (sp == this) return nullptr;
    StackContext* s = sp->readyQueue.tryDequeue();
    if (s) {
      DBG::outl(DBG::Level::Scheduling, "trySteal: ", FmtHex(this), ' ', FmtHex(s));
      stats->steal.count();
      return s;
    }
  }
}

inline StackContext* BaseProcessor::scheduleInternal() {
  StackContext* nextStack;
  if ((nextStack = tryLocal())) return nextStack;
  if ((nextStack = tryStage())) return nextStack;
  if ((nextStack = trySteal())) return nextStack;
  return nullptr;
}
#endif

void BaseProcessor::idleLoop() {
  for (;;) {
#if TESTING_LOADBALANCING
    StackContext* nextStack = cluster.getReadyStack(*this);
    if (nextStack) {
      stats->handover.count();
      yieldDirect(*nextStack);
  continue;
    }
#if TESTING_OPTIMISTIC_ISRS
    nextStack = scheduleInternal();
    if (nextStack) {
      yieldDirect(*nextStack);
  continue;
    }
    // might have gotten a token, but not a stack -> correct
    stats->correction.count();
    cluster.correctReadyStack();
#else /* TESTING_OPTIMISTIC_ISRS */
    for (;;) {
      nextStack = scheduleInternal();
      if (nextStack) break;
      Pause();
    }
    yieldDirect(*nextStack);
#endif
#else /* TESTING_LOADBALANCING */
    readyCount.P();
    StackContext* nextStack = tryLocal();
    RASSERT0(nextStack);
    yieldDirect(*nextStack);
#endif
  }
}

void BaseProcessor::enqueueResume(StackContext& s, _friend<StackContext>) {
#if TESTING_LOADBALANCING
  if (!cluster.addReadyStack(s)) enqueueDirect(s);
#else
  enqueueDirect(s);
  readyCount.V();
#endif
}

StackContext& BaseProcessor::scheduleFull(_friend<StackContext>) {
#if TESTING_LOADBALANCING
#if TESTING_IDLE_SPIN
  static const size_t SpinMax = TESTING_IDLE_SPIN;
#else
  static const size_t SpinMax = 1;
#endif
  for (size_t i = 0; i < SpinMax; i += 1) {
#if TESTING_OPTIMISTIC_ISRS
    StackContext* nextStack = scheduleInternal();
    if (nextStack) {
      cluster.reportReadyStack();
      return *nextStack;
    }
#else /* TESTING_OPTIMISTIC_ISRS */
    if (cluster.tryGetReadyStack()) {
      for (;;) {
        StackContext* nextStack = scheduleInternal();
        if (nextStack) return *nextStack;
        Pause();
      }
    }
#endif
  }
#else /* TESTING_LOADBALANCING */
  if (readyCount.tryP()) {
    StackContext* nextStack = tryLocal();
    RASSERT0(nextStack);
    return *nextStack;
  }
#endif
  return *idleStack;
}

StackContext* BaseProcessor::scheduleYield(_friend<StackContext>) {
  return tryLocal();
}

StackContext* BaseProcessor::scheduleYieldGlobal(_friend<StackContext>) {
#if TESTING_LOADBALANCING
  return scheduleInternal();
#else
  return tryLocal();
#endif
}

StackContext* BaseProcessor::schedulePreempt(StackContext* currStack, _friend<StackContext> fsc) {
  if (currStack == idleStack) return nullptr;
  return scheduleYieldGlobal(fsc);
}
