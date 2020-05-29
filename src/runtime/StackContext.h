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
#ifndef _StackContext_h_
#define _StackContext_h_ 1

#include "runtime/IntrusiveContainers.h" 
#include "runtime/Stack.h"
#include "runtime-glue/RuntimePreemption.h"

static const size_t TopPriority = 0;
static const size_t DefPriority = 1;
static const size_t LowPriority = 2;
static const size_t NumPriority = 3;

class EventScope;
class BaseSuspender;
class ResumeInfo;
class BaseProcessor;
class KernelProcessor;
class Scheduler;

#if TESTING_ENABLE_DEBUGGING
static const size_t StackLinkCount = 2;
#else
static const size_t StackLinkCount = 1;
#endif

template <size_t NUM> class StackList :
public IntrusiveList<StackContext,NUM,StackLinkCount,DoubleLink<StackContext,StackLinkCount>> {};

template <size_t NUM> class StackQueue :
public IntrusiveQueue<StackContext,NUM,StackLinkCount,DoubleLink<StackContext,StackLinkCount>> {};

template <size_t NUM> class StackMCS :
public IntrusiveQueueMCS<StackContext,NUM,StackLinkCount,DoubleLink<StackContext,StackLinkCount>> {};

template <size_t NUM> class StackMPSC :
#if TESTING_NEMESIS_READYQUEUE
public IntrusiveQueueNemesis<StackContext,NUM,StackLinkCount,DoubleLink<StackContext,StackLinkCount>> {};
#else
public IntrusiveQueueStub<StackContext,NUM,StackLinkCount,DoubleLink<StackContext,StackLinkCount>> {};
#endif

static const size_t FlexQueueLink = 0;
typedef StackList <FlexQueueLink> FlexStackList;
typedef StackQueue<FlexQueueLink> FlexStackQueue;
typedef StackMPSC <FlexQueueLink> FlexStackMPSC;

#if TESTING_ENABLE_DEBUGGING
static const size_t DebugListLink = 1;
typedef StackList<DebugListLink> GlobalStackList;
#endif

class StackContext : public DoubleLink<StackContext,StackLinkCount> {
  vaddr          stackPointer; // holds stack pointer while stack inactive
  BaseProcessor* processor;    // next resumption on this processor
  size_t         priority;     // scheduling priority
  bool           affinity;     // affinity prohibits re-staging

  size_t      volatile runState;   // runState == 0 => parked
  ResumeInfo* volatile resumeInfo; // race: unblock vs. timeout

  StackContext(const StackContext&) = delete;
  const StackContext& operator=(const StackContext&) = delete;

  // central stack switching routine
  enum SwitchCode { Idle = 'I', Yield = 'Y', Resume = 'R', Suspend = 'S', Terminate = 'T' };
  template<SwitchCode> inline void switchStack(StackContext& nextStack);

  // these routines are called immediately after the stack switch
  static void postIdle     (StackContext* prevStack);
  static void postYield    (StackContext* prevStack);
  static void postResume   (StackContext* prevStack);
  static void postSuspend  (StackContext* prevStack);
  static void postTerminate(StackContext* prevStack);

  void suspendInternal();
  void resumeInternal();
  void resumeDirect();
  static inline void yieldTo(StackContext& nextStack);
  static inline void yieldResume(StackContext& nextStack);

protected:
  // constructor/destructors can only be called by derived classes
  StackContext(BaseProcessor& proc, bool aff = false); // main constructor
  StackContext(Scheduler&, bool bg = false);           // uses delegation
  ~StackContext() {
    RASSERT(runState == 1, FmtHex(this), runState);
    RASSERT(resumeInfo == nullptr, FmtHex(this));
  }

  void initStackPointer(vaddr sp) {
    stackPointer = align_down(sp, stackAlignment);
  }

public:
  // direct switch to new stack
  void direct(ptr_t func, _friend<KernelProcessor>) __noreturn {
    stackDirect(stackPointer, func, nullptr, nullptr, nullptr);
  }

  // set up new stack and resume for concurrent execution
  void setup(ptr_t func, ptr_t p1 = nullptr, ptr_t p2 = nullptr, ptr_t p3 = nullptr) {
    stackPointer = stackInit(stackPointer, func, p1, p2, p3);
  }

  // set up new stack and resume for concurrent execution
  void start(ptr_t func, ptr_t p1 = nullptr, ptr_t p2 = nullptr, ptr_t p3 = nullptr) {
    setup(func, p1, p2, p3);
    resumeInternal();
  }

  // context switching - static -> apply to Context::CurrStack()
  static bool yield();
  static bool yieldGlobal();
  static void idleYieldTo(StackContext& nextStack, _friend<BaseProcessor>);
  static void preempt();
  static void terminate() __noreturn;

  // context switching - non-static -> restricted to BaseSuspender
  template<size_t SpinStart = 1, size_t SpinEnd = 0>
  void suspend(_friend<BaseSuspender>) {
    size_t spin = SpinStart;
    while (spin <= SpinEnd) {
      for (size_t i = 0; i < spin; i += 1) Pause();
      // resumed already? skip suspend
      size_t exp = 2;
      if (__atomic_compare_exchange_n(&runState, &exp, 1, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) return;
      spin += spin;
    }
    suspendInternal();
  }

  // if suspended (runState == 0), resume
  template<bool DirectSwitch = false>
  void resume() {
    if (__atomic_fetch_add( &runState, 1, __ATOMIC_RELAXED ) == 0) {
      if (DirectSwitch) resumeDirect();
      else resumeInternal();
    }
  }

  // set ResumeInfo to facilitate later resume race
  void setupResumeRace(ResumeInfo& ri, _friend<ResumeInfo>) {
    __atomic_store_n( &resumeInfo, &ri, __ATOMIC_RELAXED );
  }

  // race between different possible resumers -> winner cancels the other
  ResumeInfo* raceResume() {
    return __atomic_exchange_n( &resumeInfo, nullptr, __ATOMIC_RELAXED );
  }

  // change resume processor during scheduling
  void changeProcessor(BaseProcessor& rp, _friend<BaseProcessor>) {
    processor = &rp;
  }

  // hard affinity - no staging
  bool getAffinity()                  { return affinity; }
  StackContext* setAffinity(bool a)   { affinity = a; return this; }

  // priority
  size_t getPriority() const          { return priority; }
  StackContext* setPriority(size_t p) { priority = p; return this; }

  // migration
  void rebalance();
  static void migrateNow(Scheduler&);
  static void migrateNow(BaseProcessor&);
  static BaseProcessor& migrateNow(Scheduler&, _friend<EventScope>);
  static void migrateNow(BaseProcessor&, _friend<EventScope>);
};

#endif /* _StackContext_h_ */
