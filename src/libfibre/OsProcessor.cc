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
#include "libfibre/EventScope.h"
#include "libfibre/Fibre.h"
#include "libfibre/FibreCluster.h"
#include "libfibre/OsProcessor.h"

#include <limits.h>       // PTHREAD_STACK_MIN

// instance definitions for Context members declared in lfbasics.h
thread_local StackContext*  volatile Context::currStack         = nullptr;
thread_local OsProcessor*   volatile Context::currProc          = nullptr;
thread_local FibreCluster*  volatile Context::currCluster       = nullptr;
thread_local EventScope*    volatile Context::currScope         = nullptr;

// noinline routines for Context declared in lfbasics.h
void Context::setCurrStack(StackContext& s, _friend<StackContext>) { currStack = &s; }
StackContext*  Context::CurrStack()         { return currStack; }
OsProcessor*   Context::CurrProcessor()     { return currProc; }
FibreCluster*  Context::CurrCluster()       { return currCluster; }
EventScope*    Context::CurrEventScope()    { return currScope; }

inline void OsProcessor::setupContext(FibreCluster& fc) {
  Context::currProc          = this;
  Context::currCluster       = &fc;
  Context::currScope         = &fc.getEventScope();
  handoverStack = nullptr;
  maintenanceFibre = new Fibre(*this);
  maintenanceFibre->setPriority(TopPriority);
  maintenanceFibre->run(FibreCluster::maintenance, &fc, this);
#if TESTING_PROCESSOR_POLLER
  pollFibre = new PollerFibre(*Context::currScope, *this, false);
  pollFibre->start();
#endif
}

void OsProcessor::setupFakeContext(StackContext* sc, EventScope* es, _friend<PollerThread>) {
  Context::currStack = sc;
  Context::currProc = nullptr;
  Context::currCluster = nullptr;
  Context::currScope = es;
}

template<typename T>
inline void OsProcessor::idleLoopCreateFibre(void (*initFunc)(T*, _friend<OsProcessor>), T* arg) {
  setupContext(reinterpret_cast<FibreCluster&>(cluster));
  // idle loop takes over pthread stack - create fibre without stack
  Context::currStack = idleStack = new Fibre(*this, _friend<OsProcessor>());
  if (initFunc) initFunc(arg, _friend<OsProcessor>());
  if (initFibre) yieldDirect(*initFibre);
  running.post();
  idleLoop();
  reinterpret_cast<Fibre*>(idleStack)->endDirect(_friend<OsProcessor>());
}

void OsProcessor::idleLoopStartFibre(OsProcessor* sp) {
  sp->running.post();
  sp->idleLoop();
}

void* OsProcessor::idleLoopStartPthread(void* sp) {
  OsProcessor* This = reinterpret_cast<OsProcessor*>(sp);
  This->idleLoopCreateFibre();
  return nullptr;
}

void* OsProcessor::idleLoopStartEventScope(void* sp) {
  OsProcessor* This = reinterpret_cast<OsProcessor*>(sp);
  This->idleLoopCreateFibre(EventScope::split, reinterpret_cast<EventScope*>(This->idleStack));
  return nullptr;
}

inline void OsProcessor::startPthreadHelper(funcptr1_t idleLoopStarter) {
  cluster.addProcessor(*this); // potentially blocking caller fibre 
  pthread_attr_t attr;
  SYSCALL(pthread_attr_init(&attr));
  SYSCALL(pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED));
#if __linux__ // FreeBSD jemalloc segfaults when trying to use minimum stack
  SYSCALL(pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN));
#endif
  SYSCALL(pthread_create(&sysThread, &attr, idleLoopStarter, this));
  SYSCALL(pthread_attr_destroy(&attr));
}

OsProcessor::OsProcessor(funcvoid1_t initFunc, ptr_t arg) : OsProcessor(::CurrCluster(), initFunc, arg) {}

OsProcessor::OsProcessor(FibreCluster& fc, funcvoid1_t initFunc, ptr_t arg) : BaseProcessor(fc), initFibre(nullptr) {
  GENASSERT(&::CurrEventScope() == &fc.getEventScope());
  if (initFunc) {
    initFibre = new Fibre(*this);
    initFibre->setup((ptr_t)initFunc, (ptr_t)arg);
  }
  startPthreadHelper(idleLoopStartPthread);    // create pthread running idleLoop
}

OsProcessor::OsProcessor(FibreCluster& fc, EventScope& scope, _friend<EventScope>) : BaseProcessor(fc), initFibre(nullptr) {
  idleStack = (StackContext*)&scope; // HACK
  startPthreadHelper(idleLoopStartEventScope); // create pthread running idleLoop
}

OsProcessor::OsProcessor(FibreCluster& fc, _friend<_Bootstrapper>) : BaseProcessor(fc), initFibre(nullptr) {
  sysThread = pthread_self();
  setupContext(fc);
  idleStack = new Fibre(*this);
  idleStack->setup((ptr_t)idleLoopStartFibre, this);
  // main fibre takes over pthread stack - create fibre without stack
  Fibre* mainFibre = new Fibre(*this, _friend<OsProcessor>());
  Context::currStack = mainFibre;
  cluster.addProcessor(*this); // first processor -> should not block, but need currStack set for ringLock
}
