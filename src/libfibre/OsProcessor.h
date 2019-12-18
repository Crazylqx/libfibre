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

typedef FifoSemaphore<InternalLock> FibreSemaphore;
typedef Mutex<InternalLock>         FibreMutex;
typedef Condition<FibreMutex>       FibreCondition;
typedef LockRW<InternalLock>        FibreLockRW;
typedef Barrier<InternalLock>       FibreBarrier;

class _Bootstrapper;
class BaseThreadPoller;
class Fibre;
class PollerFibre;

/**
 An OsProcessor object represents an OS-level execution thread (pthread).
*/
class OsProcessor : public BaseProcessor {
  pthread_t               sysThread;
  Fibre*                  initFibre;
  Fibre*                  maintenanceFibre;
  Benaphore<OsSemaphore>  haltNotify; // benaphore better for spinning
  StackContext*           handoverStack;
#if TESTING_PROCESSOR_POLLER
  PollerFibre*            pollFibre;
#endif

  inline void  setupContext();
  static void  idleLoopStartFibre(OsProcessor* This);
  inline void  idleLoopCreatePthread(funcvoid1_t initFunc = nullptr, ptr_t arg = nullptr);
  static ptr_t idleLoopStartPthread(OsProcessor* This);

public:
  /** Constructor: create OsProcessor in current Cluster. */
  OsProcessor(funcvoid1_t initFunc = nullptr, ptr_t arg = nullptr);
  /** Constructor: create OsProcessor in specified Cluster. */
  OsProcessor(Cluster& cluster, funcvoid1_t initFunc = nullptr, ptr_t arg = nullptr);

  // dedicated constructor for event scope: pthread executes initFunc before idle
  OsProcessor(Cluster& cluster, funcvoid1_t initFunc, ptr_t arg, _friend<EventScope>);
  // dedicated constructor for bootstrap: pthread becomes mainFibre
  OsProcessor(Cluster& cluster, _friend<_Bootstrapper>);

  // fake context for poller pthread, needed 'currScope' for timer handling
  static void setupFakeContext(StackContext* sc, EventScope* es, _friend<BaseThreadPoller>);

  ~OsProcessor() { RABORT("Cannot delete OsProcessor"); }

#if TESTING_PROCESSOR_POLLER
  PollerFibre& getPoller() { RASSERT0(pollFibre); return *pollFibre; }
#endif

  pthread_t getSysID() { return sysThread; }

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
