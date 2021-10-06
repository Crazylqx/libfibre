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
#include "libfibre/Cluster.h"

#include <limits.h> // PTHREAD_STACK_MIN

namespace Context {

static thread_local Fred*          currFred     = nullptr;
static thread_local BaseProcessor* currProc     = nullptr;
static thread_local Cluster*       currCluster  = nullptr;
static thread_local EventScope*    currScope    = nullptr;

Fred*          CurrFred()       { RASSERT0(currFred);    return  currFred; }
BaseProcessor& CurrProcessor()  { RASSERT0(currProc);    return *currProc; }
Cluster&       CurrCluster()    { RASSERT0(currCluster); return *currCluster; }
EventScope&    CurrEventScope() { RASSERT0(currScope);   return *currScope; }

void setCurrFred(Fred& f, _friend<Fred>) { currFred = &f; }

void install(Fibre* fib, BaseProcessor* bp, Cluster* cl, EventScope* es, _friend<Cluster>) {
  currFred    = fib;
  currProc    = bp;
  currCluster = cl;
  currScope   = es;
}

void installFake(EventScope* es, _friend<BaseThreadPoller>) {
  currFred  = (Fred*)0xdeadbeef;
  currScope = es;
}

} // namespace Context

#if TESTING_IO_URING
bool RuntimeWorkerPoll(BaseProcessor& proc) {
  return Cluster::pollWorker(proc);
}
bool RuntimeWorkerTrySuspend(BaseProcessor& proc) {
  return Cluster::trySuspendWorker(proc);
}
void RuntimeWorkerSuspend(BaseProcessor& proc) {
  Cluster::suspendWorker(proc);
}
void RuntimeWorkerResume(BaseProcessor& proc) {
  Cluster::resumeWorker(proc);
}
#endif

inline void Cluster::setupWorker(Fibre* fibre, Worker* worker) {
#ifdef SPLIT_STACK
  stack_t ss = { .ss_sp = worker->sigStack, .ss_flags = 0, .ss_size = SIGSTKSZ };
  SYSCALL(sigaltstack(&ss, nullptr));
  int off = 0; // do not block signals (blocking signals is slow!)
  __splitstack_block_signals(&off, nullptr);
#endif
  worker->sysThreadId = pthread_self();
  Context::install(fibre, worker, this, &scope, _friend<Cluster>());
#if TESTING_IO_URING
  worker->iouring = new IOUring(worker, "W-IOUring ");
#endif
#if TESTING_WORKER_POLLER
  worker->workerPoller = new PollerFibre(scope, *worker, worker, "W-Poller  ", _friend<Cluster>(), false);
  worker->workerPoller->start();
#endif
}

void Cluster::initDummy(ptr_t) {}

void Cluster::fibreHelper(Worker* worker) {
  worker->runIdleLoop();
}

void* Cluster::threadHelper(Argpack* args) {
  args->cluster->registerIdleWorker(args->worker, args->initFibre);
  return nullptr;
}

inline void Cluster::registerIdleWorker(Worker* worker, Fibre* initFibre) {
  Fibre* idleFibre = new Fibre(*worker, _friend<Cluster>(), 0); // idle fibre on pthread stack
  setupWorker(idleFibre, worker);
  worker->setIdleLoop(idleFibre);
  Worker::yieldDirect(*initFibre);                           // run init fibre right away
  worker->runIdleLoop();
  idleFibre->endDirect(_friend<Cluster>());
}

void Cluster::preFork(_friend<EventScope>) {
  ScopedLock<WorkerLock> sl(ringLock);
  RASSERT(ringCount == 1, ringCount);
}

void Cluster::postFork1(cptr_t parent, _friend<EventScope>) {
  new (stats) FredStats::ClusterStats(this, parent);
  for (size_t p = 0; p < iPollCount; p += 1) {
    iPollVec[p].~PollerType();
    new (&iPollVec[p]) PollerType(scope, stagingProc, this, "I-Poller   ", _friend<Cluster>());
  }
  for (size_t p = 0; p < oPollCount; p += 1) {
    oPollVec[p].~PollerType();
    new (&oPollVec[p]) PollerType(scope, stagingProc, this, "O-Poller   ", _friend<Cluster>());
  }
#if TESTING_IO_URING
  CurrWorker().iouring->~IOUring();
  new (CurrWorker().iouring) IOUring(&CurrWorker(), "W-IOUring ");
#endif
#if TESTING_WORKER_POLLER
  CurrWorker().workerPoller->~PollerFibre();
  new (CurrWorker().workerPoller) PollerFibre(Context::CurrEventScope(), CurrWorker(), &CurrWorker(), "W-Poller  ", _friend<Cluster>(), false);
#endif
}

void Cluster::postFork2(_friend<EventScope>) {
  start();
#if TESTING_WORKER_POLLER
  CurrWorker().workerPoller->start();
#endif
}

Fibre* Cluster::registerWorker(_friend<EventScope>) {
  Worker* worker = new Worker(*this);
  Fibre* mainFibre = new Fibre(*worker, _friend<Cluster>(), 0); // caller continues on pthread stack
  setupWorker(mainFibre, worker);
  Fibre* idleFibre = new Fibre(*worker, _friend<Cluster>()); // idle fibre on new stack
  idleFibre->setup((ptr_t)fibreHelper, worker);              // set up idle fibre for execution
  worker->setIdleLoop(idleFibre);
  return mainFibre;
}

Cluster::Worker& Cluster::addWorker(funcvoid1_t initFunc, ptr_t initArg) {
  Worker* worker = new Worker(*this);
  Fibre* initFibre = new Fibre(*worker, _friend<Cluster>());
  if (initFunc) {   // run init routine in dedicated fibre, so it can block
    initFibre->setup((ptr_t)initFunc, initArg);
  } else {
    initFibre->setup((ptr_t)initDummy, nullptr);
  }
  Argpack args = { this, worker, initFibre };
  pthread_t tid;
  pthread_attr_t attr;
  SYSCALL(pthread_attr_init(&attr));
  SYSCALL(pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED));
#if __linux__       // FreeBSD jemalloc segfaults when trying to use minimum stack
  SYSCALL(pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN));
#endif
  SYSCALL(pthread_create(&tid, &attr, (funcptr1_t)threadHelper, &args));
  SYSCALL(pthread_attr_destroy(&attr));
  delete initFibre; // also synchronization that 'args' not needed anymore
  return *worker;
}

void Cluster::pause() {
  ringLock.acquire();
  stats->pause.count(ringCount);
  for (BaseProcessor* proc = placeProc;;) {
    if (proc != &Context::CurrProcessor()) {
      Fibre* f = new Fibre(*proc, _friend<Cluster>());
      f->setPriority(Fred::TopPriority);
      f->run(pauseOperation, this);
      pauseFibres.push_back(f);
    }
    proc = ProcessorRingGlobal::next(*proc);
    if (proc == placeProc) break;
  }
  RASSERT(pauseFibres.size() == ringCount-1, pauseFibres.size(), ringCount-1);
  for (size_t p = 1; p < ringCount; p += 1) pauseConfirmSem.P();
}

void Cluster::resume() {
  for (size_t p = 1; p < ringCount; p += 1) pauseSem.V();
  for (auto f : pauseFibres) delete f;
  pauseFibres.clear();
  ringLock.release();
}

void Cluster::pauseOperation(Cluster* cl) {
  cl->pauseConfirmSem.V();
  cl->pauseSem.P();
}
