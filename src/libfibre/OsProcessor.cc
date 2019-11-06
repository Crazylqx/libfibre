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
#include "libfibre/Cluster.h"
#include "libfibre/EventScope.h"
#include "libfibre/Fibre.h"
#include "libfibre/OsProcessor.h"

#include <limits.h>       // PTHREAD_STACK_MIN

inline void OsProcessor::setupContext(Cluster& cl) {
  Context::currProc = this;
  Context::currCluster = &cl;
  Context::currScope = &cl.getEventScope();
  handoverStack = nullptr;
  maintenanceFibre = new Fibre(*this);
  maintenanceFibre->setPriority(TopPriority);
  maintenanceFibre->run(Cluster::maintenance, &cl);
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
  setupContext(reinterpret_cast<Cluster&>(scheduler));
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
  scheduler.addProcessor(*this); // potentially blocking caller fibre
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

OsProcessor::OsProcessor(Cluster& cl, funcvoid1_t initFunc, ptr_t arg) : BaseProcessor(cl), initFibre(nullptr) {
  RASSERT0(&::CurrEventScope() == &cl.getEventScope());
  if (initFunc) {
    initFibre = new Fibre(*this);
    initFibre->setup((ptr_t)initFunc, (ptr_t)arg);
  }
  startPthreadHelper(idleLoopStartPthread);    // create pthread running idleLoop
}

OsProcessor::OsProcessor(Cluster& cl, EventScope& scope, _friend<EventScope>) : BaseProcessor(cl), initFibre(nullptr) {
  idleStack = (StackContext*)&scope; // HACK
  startPthreadHelper(idleLoopStartEventScope); // create pthread running idleLoop
}

OsProcessor::OsProcessor(Cluster& cl, _friend<_Bootstrapper>) : BaseProcessor(cl), initFibre(nullptr) {
  sysThread = pthread_self();
  setupContext(cl);
  idleStack = new Fibre(*this);
  idleStack->setup((ptr_t)idleLoopStartFibre, this);
  // main fibre takes over pthread stack - create fibre without stack
  Fibre* mainFibre = new Fibre(*this, _friend<OsProcessor>());
  Context::currStack = mainFibre;
  scheduler.addProcessor(*this); // first processor -> should not block, but need currStack set for ringLock
}
