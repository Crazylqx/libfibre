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

inline void OsProcessor::setupContext() {
  Cluster& cl = reinterpret_cast<Cluster&>(scheduler);
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

void OsProcessor::idleLoopStartFibre(OsProcessor* This) {
  This->idleLoop();
}

inline void OsProcessor::idleLoopCreatePthread(funcvoid1_t initFunc, ptr_t arg) {
  scheduler.addProcessor(*this); // potentially blocking caller fibre
  if (initFunc) {
    initFibre = new Fibre(*this);
    initFibre->setup((ptr_t)initFunc, arg);
  }
  pthread_attr_t attr;
  SYSCALL(pthread_attr_init(&attr));
  SYSCALL(pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED));
#if __linux__ // FreeBSD jemalloc segfaults when trying to use minimum stack
  SYSCALL(pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN));
#endif
  SYSCALL(pthread_create(&sysThread, &attr, (funcptr1_t)idleLoopStartPthread, this));
  SYSCALL(pthread_attr_destroy(&attr));
}

ptr_t OsProcessor::idleLoopStartPthread(OsProcessor* This) {
  This->setupContext();
  // idle loop takes over pthread stack - create fibre without stack
  Context::currStack = This->idleStack = new Fibre(*This, _friend<OsProcessor>());
  if (This->initFibre) This->yieldDirect(*This->initFibre);
  This->idleLoop();
  reinterpret_cast<Fibre*>(This->idleStack)->endDirect(_friend<OsProcessor>());
  return nullptr;
}

OsProcessor::OsProcessor(funcvoid1_t initFunc, ptr_t arg) : OsProcessor(::CurrCluster(), initFunc, arg) {}

OsProcessor::OsProcessor(Cluster& cl, funcvoid1_t initFunc, ptr_t arg) : BaseProcessor(cl), initFibre(nullptr) {
  RASSERT0(&::CurrEventScope() == &cl.getEventScope());
  idleLoopCreatePthread(initFunc, arg); // create pthread running idleLoop
}

OsProcessor::OsProcessor(Cluster& cl, funcvoid1_t initFunc, ptr_t arg, _friend<EventScope>) : BaseProcessor(cl), initFibre(nullptr) {
  idleLoopCreatePthread(initFunc, arg); // create pthread running idleLoop
}

OsProcessor::OsProcessor(Cluster& cl, _friend<_Bootstrapper>) : BaseProcessor(cl), initFibre(nullptr) {
  sysThread = pthread_self();
  setupContext();
  idleStack = new Fibre(*this);
  idleStack->setup((ptr_t)idleLoopStartFibre, this);
  // main fibre takes over pthread stack - create fibre without stack
  Fibre* mainFibre = new Fibre(*this, _friend<OsProcessor>());
  Context::currStack = mainFibre;
  scheduler.addProcessor(*this); // first processor -> should not block, but need currStack set for ringLock
}

void OsProcessor::waitUntilRunning() {
  RASSERT0(initFibre);
  delete initFibre;
  initFibre = nullptr;
}
