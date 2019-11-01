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
#ifndef _OsProcessor_h_
#define _OsProcessor_h_ 1

#include "runtime/Benaphore.h"
#include "runtime/BaseProcessor.h"
#include "runtime/BlockingSync.h"
#include "libfibre/Poller.h"

typedef FifoSemaphore<InternalLock,false> FibreSemaphore;
typedef FifoSemaphore<InternalLock,true>  FibreBinarySemaphore;
typedef Mutex<InternalLock>               FibreMutex;
typedef Condition<FibreMutex>             FibreCondition;
typedef LockRW<InternalLock>              FibreLockRW;
typedef Barrier<InternalLock>             FibreBarrier;

class OsProcessor : public Context, public BaseProcessor {
  pthread_t               sysThread;
  SyncPoint<InternalLock> running;
  Fibre*                  initFibre;
  Fibre*                  maintenanceFibre;
  Benaphore<OsSemaphore>  haltNotify; // benaphore better for spinning
  StackContext*           handoverStack;
#if TESTING_PROCESSOR_POLLER
  PollerFibre*            pollFibre;
#endif

  inline void setupContext(FibreCluster& fc);

  template<typename T = void>
  inline void  idleLoopCreateFibre(void (*initFunc)(T*, _friend<OsProcessor>) = nullptr, T* arg = nullptr);
  static void  idleLoopStartFibre(OsProcessor*);
  static void* idleLoopStartPthread(void*);
  static void* idleLoopStartEventScope(void*);

  inline void startPthreadHelper(funcptr1_t idleLoopStarter);

public:
  // regular constructors: create pthread and use for idle loop
  OsProcessor(funcvoid1_t initFunc = nullptr, ptr_t arg = nullptr);
  OsProcessor(FibreCluster& cluster, funcvoid1_t initFunc = nullptr, ptr_t arg = nullptr);
  // dedicated constructor for event scope: pthread executes EventScope::split before idle
  OsProcessor(FibreCluster& cluster, EventScope& scope, _friend<EventScope>);
  // dedicated constructor for bootstrap: pthread becomes mainFibre
  OsProcessor(FibreCluster& cluster, _friend<_Bootstrapper>);

  ~OsProcessor() { RABORT("Cannot delete OsProcessor"); }

  // dedicated support routine to set up dummy context for poller pthreads
  static void setupFakeContext(StackContext* sc, EventScope* es, _friend<PollerThread>);

#if TESTING_PROCESSOR_POLLER
  BasePoller& getPoller() { RASSERT0(pollFibre); return *pollFibre; }
#endif

  pthread_t getSysID() { return sysThread; }
  void waitUntilRunning() { running.wait(); }

  StackContext* suspend() {
#if TESTING_HALT_SPIN
    static const size_t SpinMax = TESTING_HALT_SPIN;
    for (size_t i = 0; i < SpinMax; i += 1) {
      if fastpath(haltNotify.tryP()) return handoverStack;
      Pause();
    }
#endif
    stats->idle.count();
    haltNotify.P();
    return handoverStack;
  }

  void resume(StackContext* sc = nullptr) {
    stats->wake.count();
    handoverStack = sc;
    haltNotify.V();
  }
};

#endif /* _OsProcessor_h_ */
