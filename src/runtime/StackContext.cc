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
#include "runtime/Scheduler.h"
#include "runtime/StackContext.h"
#include "runtime-glue/RuntimeStack.h"

StackContext::StackContext(BaseProcessor& proc, bool aff)
: stackPointer(0), processor(&proc), priority(DefPriority), affinity(aff), runState(1), resumeInfo(nullptr) {
#if TESTING_SHARED_READYQUEUE
  affinity = true;
#endif
  processor->addStack(_friend<StackContext>());
}

StackContext::StackContext(Scheduler& scheduler, bool bg)
: StackContext(scheduler.placement(_friend<StackContext>(), bg), bg) {}

template<StackContext::SwitchCode Code>
inline void StackContext::switchStack(StackContext& nextStack) {
  // various checks
  static_assert(Code == Idle || Code == Yield || Code == Migrate || Code == Suspend || Code == Terminate, "Illegal SwitchCode");
  CHECK_PREEMPTION(0);
  RASSERT(this == Context::CurrStack() && this != &nextStack, FmtHex(this), ' ', FmtHex(Context::CurrStack()), ' ', FmtHex(&nextStack));

  // context switch
  DBG::outl(DBG::Level::Scheduling, "Stack switch <", char(Code), "> on ", FmtHex(&Context::CurrProcessor()),": ", FmtHex(this), " (to ", FmtHex(processor), ") -> ", FmtHex(&nextStack));
  RuntimePreStackSwitch(*this, nextStack, _friend<StackContext>());
  switch (Code) {
    case Idle:      stackSwitch(this, postIdle,      &stackPointer, nextStack.stackPointer); break;
    case Yield:     stackSwitch(this, postYield,     &stackPointer, nextStack.stackPointer); break;
    case Migrate:   stackSwitch(this, postMigrate,   &stackPointer, nextStack.stackPointer); break;
    case Suspend:   stackSwitch(this, postSuspend,   &stackPointer, nextStack.stackPointer); break;
    case Terminate: stackSwitch(this, postTerminate, &stackPointer, nextStack.stackPointer); break;
  }
  stackPointer = 0;                // mark stack in use for gdb
  RuntimePostStackSwitch(*this, _friend<StackContext>()); // RT-specific functionality
}

// idle stack -> do nothing
void StackContext::postIdle(StackContext*) {
  CHECK_PREEMPTION(0);
}

// yield -> resume right away
void StackContext::postYield(StackContext* prevStack) {
  CHECK_PREEMPTION(0);
  prevStack->processor->enqueueDirect(*prevStack, _friend<StackContext>());
}

// yield -> resume right away
void StackContext::postMigrate(StackContext* prevStack) {
  CHECK_PREEMPTION(0);
  prevStack->resumeInternal();
}

// if resumption already triggered -> resume right away
void StackContext::postSuspend(StackContext* prevStack) {
  CHECK_PREEMPTION(0);
  // check if previous stack is already resumed?
  if (__atomic_sub_fetch( &prevStack->runState, 1, __ATOMIC_RELAXED ) > 0) prevStack->resumeInternal();
}

// destroy stack
void StackContext::postTerminate(StackContext* prevStack) {
  CHECK_PREEMPTION(0);
  prevStack->processor->removeStack(_friend<StackContext>());
  RuntimeStackDestroy(*prevStack, _friend<StackContext>());
}

// a new thread/stack starts in stubInit() and then jumps to this routine
extern "C" void invokeStack(funcvoid3_t func, ptr_t arg1, ptr_t arg2, ptr_t arg3) {
  CHECK_PREEMPTION(0);
  RuntimeEnablePreemption();
  func(arg1, arg2, arg3);
  RuntimeDisablePreemption();
  StackContext::terminate();
}

void StackContext::idleYieldTo(StackContext& nextStack, _friend<BaseProcessor>) {
  CHECK_PREEMPTION(1);          // expect preemption still enabled
  RuntimeDisablePreemption();
  Context::CurrStack()->switchStack<Idle>(nextStack);
  RuntimeEnablePreemption();
}

bool StackContext::yield() {
  CHECK_PREEMPTION(1);          // expect preemption still enabled
  RuntimeDisablePreemption();
  StackContext* nextStack = Context::CurrProcessor().scheduleYield(_friend<StackContext>());
  if (nextStack) Context::CurrStack()->switchStack<Yield>(*nextStack);
  RuntimeEnablePreemption();
  return nextStack;
}

bool StackContext::yieldGlobal() {
  CHECK_PREEMPTION(1);          // expect preemption still enabled
  RuntimeDisablePreemption();
  StackContext* nextStack = Context::CurrProcessor().scheduleYieldGlobal(_friend<StackContext>());
  if (nextStack) Context::CurrStack()->switchStack<Yield>(*nextStack);
  RuntimeEnablePreemption();
  return nextStack;
}

void StackContext::preempt() {
  CHECK_PREEMPTION(0);
  StackContext* currStack = Context::CurrStack();
  StackContext* nextStack = Context::CurrProcessor().schedulePreempt(currStack, _friend<StackContext>());
  if (nextStack) currStack->switchStack<Yield>(*nextStack);
}

void StackContext::terminate() {
  CHECK_PREEMPTION(0);
  Context::CurrStack()->switchStack<Terminate>(Context::CurrProcessor().scheduleFull(_friend<StackContext>()));
  unreachable();
}

void StackContext::suspendInternal() {
  switchStack<Suspend>(Context::CurrProcessor().scheduleFull(_friend<StackContext>()));
}

void StackContext::resumeInternal() {
  processor->enqueueResume(*this, _friend<StackContext>());
}

void StackContext::changeProcessor(BaseProcessor& p) {
  processor->removeStack(_friend<StackContext>());
  processor = &p;
  processor->addStack(_friend<StackContext>());
}

void StackContext::rebalance() {
  if (!affinity) changeProcessor(Context::CurrProcessor().getScheduler().placement(_friend<StackContext>(), true));
}

// migrate to scheduler; adjust stackCounts, clear affinity
void StackContext::migrateNow(Scheduler& scheduler) {
  migrateNow(scheduler.placement(_friend<StackContext>(), true));
}

// migrate to proessor; adjust stackCounts, clear affinity
void StackContext::migrateNow(BaseProcessor& proc) {
  StackContext* sc = Context::CurrStack();
  sc->affinity = false;
  sc->changeProcessor(proc);
  RuntimeDisablePreemption();
  sc->switchStack<Migrate>(Context::CurrProcessor().scheduleFull(_friend<StackContext>()));
  RuntimeEnablePreemption();
}

// migrate to scheduler (for disk I/O), don't change stackCount or affinity
BaseProcessor& StackContext::migrateNow(Scheduler& scheduler, _friend<EventScope>) {
  StackContext* sc = Context::CurrStack();
  BaseProcessor* proc = sc->processor;
  sc->processor = &scheduler.placement(_friend<StackContext>(), true);
  RuntimeDisablePreemption();
  sc->switchStack<Migrate>(Context::CurrProcessor().scheduleFull(_friend<StackContext>()));
  RuntimeEnablePreemption();
  return *proc;
}

// migrate back to previous processor (after disk I/O), don't change stackCount or affinity
void StackContext::migrateNow(BaseProcessor& proc, _friend<EventScope>) {
  StackContext* sc = Context::CurrStack();
  sc->processor = &proc;
  RuntimeDisablePreemption();
  sc->switchStack<Migrate>(Context::CurrProcessor().scheduleFull(_friend<StackContext>()));
  RuntimeEnablePreemption();
}
