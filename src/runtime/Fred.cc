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
#include "runtime/Fred.h"
#include "runtime-glue/RuntimeFred.h"

Fred::Fred(BaseProcessor& proc, Affinity affinity)
: stackPointer(0), processor(&proc), priority(DefaultPriority), affinity(affinity), runState(Running) {
#if TESTING_SHARED_READYQUEUE
  this->affinity = FixedAffinity;
#endif
  processor->countFredCreated();
}

Fred::Fred(Scheduler& scheduler, bool backgroundQueue)
: Fred(scheduler.placement(_friend<Fred>(), backgroundQueue), backgroundQueue ? FixedAffinity : DefaultAffinity) {}

template<Fred::SwitchCode Code>
inline void Fred::switchFred(Fred& nextFred) {
  // various checks
  static_assert(Code == Idle || Code == Yield || Code == Resume || Code == Suspend || Code == Terminate, "Illegal SwitchCode");
  CHECK_PREEMPTION(0);
  RASSERT(this == Context::CurrFred() && this != &nextFred, FmtHex(this), ' ', FmtHex(Context::CurrFred()), ' ', FmtHex(&nextFred));

  // context switch
  DBG::outl(DBG::Level::Scheduling, "Fred switch <", char(Code), "> on ", FmtHex(&Context::CurrProcessor()),": ", FmtHex(this), " (to ", FmtHex(processor), ") -> ", FmtHex(&nextFred));
  RuntimePreFredSwitch(*this, nextFred, _friend<Fred>());
  switch (Code) {
    case Idle:      stackSwitch(this, postIdle,      &stackPointer, nextFred.stackPointer); break;
    case Yield:     stackSwitch(this, postYield,     &stackPointer, nextFred.stackPointer); break;
    case Resume:    stackSwitch(this, postResume,    &stackPointer, nextFred.stackPointer); break;
    case Suspend:   stackSwitch(this, postSuspend,   &stackPointer, nextFred.stackPointer); break;
    case Terminate: stackSwitch(this, postTerminate, &stackPointer, nextFred.stackPointer); break;
  }
  stackPointer = 0;                              // mark stack in use for gdb
  RuntimePostFredSwitch(*this, _friend<Fred>()); // runtime-specific functionality
}

// idle fred -> do nothing
void Fred::postIdle(Fred*) {
  CHECK_PREEMPTION(0);
}

// yield -> resume right away
void Fred::postYield(Fred* prevFred) {
  CHECK_PREEMPTION(0);
  prevFred->processor->enqueueYield(*prevFred, _friend<Fred>());
}

// yield -> resume right away
void Fred::postResume(Fred* prevFred) {
  CHECK_PREEMPTION(0);
  prevFred->resumeInternal();
}

// if resumption already triggered -> resume right away
void Fred::postSuspend(Fred* prevFred) {
  CHECK_PREEMPTION(0);
  size_t prev = __atomic_fetch_sub(&prevFred->runState, RunState(1), __ATOMIC_SEQ_CST);
  if (prev == ResumedEarly) prevFred->resumeInternal(); // ResumedEarly -> Running
  else RASSERT(prev == Running, prev);                  // Running -> Parked
}

// destroy fred
void Fred::postTerminate(Fred* prevFred) {
  CHECK_PREEMPTION(0);
  RuntimeFredDestroy(*prevFred, _friend<Fred>());
}

// a new fred starts in stubInit() and then jumps to this routine
extern "C" void invokeFred(funcvoid3_t func, ptr_t arg1, ptr_t arg2, ptr_t arg3) {
  CHECK_PREEMPTION(0);
  RuntimeEnablePreemption();
  RuntimeStartFred(func, arg1, arg2, arg3);
  RuntimeDisablePreemption();
  Fred::terminate();
}

void Fred::resumeDirect() {
  Context::CurrFred()->yieldResume(*this);
}

void Fred::resumeInternal() {
  processor->enqueueResume(*this, _friend<Fred>());
}

void Fred::suspendInternal() {
  switchFred<Suspend>(Context::CurrProcessor().scheduleFull(_friend<Fred>()));
}

inline void Fred::yieldTo(Fred& nextFred) {
  CHECK_PREEMPTION(1);           // expect preemption still enabled
  RuntimeDisablePreemption();
  switchFred<Yield>(nextFred);   // yield without updating fredCounter
  RuntimeEnablePreemption();
}

inline void Fred::yieldResume(Fred& nextFred) {
  CHECK_PREEMPTION(1);           // expect preemption still enabled
  RuntimeDisablePreemption();
  switchFred<Resume>(nextFred);  // yield and increase fredCounter
  RuntimeEnablePreemption();
}

inline void Fred::yieldForce() { // force schedule (even to idle) and yiled
  yieldResume(Context::CurrProcessor().scheduleFull(_friend<Fred>()));
}

bool Fred::yield() {
  Fred* nextFred = Context::CurrProcessor().scheduleYield(_friend<Fred>());
  if (nextFred) Context::CurrFred()->yieldTo(*nextFred);
  return nextFred;
}

bool Fred::yieldGlobal() {
  Fred* nextFred = Context::CurrProcessor().scheduleYieldGlobal(_friend<Fred>());
  if (nextFred) Context::CurrFred()->yieldTo(*nextFred);
  return nextFred;
}

void Fred::idleYieldTo(Fred& nextFred, _friend<BaseProcessor>) {
  CHECK_PREEMPTION(1);          // expect preemption still enabled
  RuntimeDisablePreemption();
  Context::CurrFred()->switchFred<Idle>(nextFred);
  RuntimeEnablePreemption();
}

void Fred::preempt() {
  CHECK_PREEMPTION(0);
  Fred* currFred = Context::CurrFred();
  Fred* nextFred = Context::CurrProcessor().schedulePreempt(currFred, _friend<Fred>());
  if (nextFred) currFred->switchFred<Yield>(*nextFred);
}

void Fred::terminate() {
  CHECK_PREEMPTION(0);
  Context::CurrFred()->switchFred<Terminate>(Context::CurrProcessor().scheduleFull(_friend<Fred>()));
  RABORT0();
}

void Fred::rebalance() {
  if (affinity != FixedAffinity) processor = &Context::CurrProcessor().getScheduler().placement(_friend<Fred>(), true);
}

// migrate to scheduler; clear affinity
void Fred::migrateNow(Scheduler& scheduler) {
  Fred* f = Context::CurrFred();
  f->affinity = DefaultAffinity;
  f->processor = &scheduler.placement(_friend<Fred>(), true);
  f->yieldForce();
}

void Fred::migrateLocal(BaseProcessor& proc) {
#if TESTING_SHARED_READYQUEUE
  return;
#endif
  Fred* f = Context::CurrFred();
  RASSERT0(&f->processor->getScheduler() == &proc.getScheduler());
  f->processor = &proc;
  if (!f->yieldGlobal()) f->yieldForce();
}

// migrate to scheduler (for disk I/O), don't change affinity
BaseProcessor& Fred::migrateNow(Scheduler& scheduler, _friend<EventScope>) {
  Fred* f = Context::CurrFred();
  BaseProcessor* proc = f->processor;
  f->processor = &scheduler.placement(_friend<Fred>(), true);
  f->yieldForce();
  return *proc;
}

// migrate back to previous processor (after disk I/O), don't change affinity
void Fred::migrateNow(BaseProcessor& proc, _friend<EventScope>) {
  Fred* f = Context::CurrFred();
  f->processor = &proc;
  f->yieldForce();
}
