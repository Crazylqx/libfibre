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
#ifndef _EventScope_h_
#define _EventScope_h_ 1

/** @file */

#include "libfibre/Fibre.h"
#include "libfibre/Cluster.h"

#include <fcntl.h>        // O_NONBLOCK
#include <limits.h>       // PTHREAD_STACK_MIN
#include <unistd.h>       // close
#include <sys/resource.h> // getrlimit
#include <sys/types.h>
#include <sys/socket.h>

/**
 An EventScope object holds a set of Clusters and provides a common I/O
 scope.  Multiple EventScope objects can be used to take advantage of
 partitioned kernel file descriptor tables on Linux.
*/
class EventScope {
  // A vector for FDs works well here in principle, because POSIX guarantees lowest-numbered FDs:
  // http://pubs.opengroup.org/onlinepubs/9699919799/functions/V2_chap02.html#tag_15_14
  // A fixed-size array based on 'getrlimit' is somewhat brute-force, but simple and fast.
  typedef LockedSemaphore<WorkerLock,true> SyncSem;
  struct SyncFD {
    SyncSem     iSem;
    SyncSem     oSem;
    BasePoller* volatile iPoller;
    BasePoller* volatile oPoller;
    bool        volatile blocking;
    SyncFD() : iPoller(nullptr), oPoller(nullptr), blocking(false) {}
  } *fdSyncVector;

  int fdCount;

  EventScope*   parentScope;
  MasterPoller* masterPoller; // runs without cluster
  TimerQueue    timerQueue;   // scope-global timer queue

  // on Linux, file I/O cannot be monitored via select/poll/epoll
  // therefore, all file operations are executed on dedicated processor(s)
  Cluster*      diskCluster;

  // main fibre, cluster
  Fibre*        mainFibre;
  Cluster*      mainCluster;

  // simple kludge to provide event-scope-local data
  void*         clientData;

  // TODO: not available until cluster deletion implemented
  ~EventScope() {
    delete mainFibre;
    delete mainCluster;
    masterPoller->terminate(_friend<EventScope>());
    delete masterPoller;
    delete[] fdSyncVector;
  }

  static void cloneInternal(EventScope* This) {
    This->initSync();
    RASSERT0(This->parentScope);
#if __linux__
    for (int f = 0; f < This->fdCount; f += 1) This->fdSyncVector[f].blocking = This->parentScope->fdSyncVector[f].blocking;
    SYSCALL(unshare(CLONE_FILES));
#else
    (void)This->parentScope;
#endif
    This->start();
  }

  EventScope(size_t pollerCount, EventScope* ps = nullptr) : parentScope(ps), timerQueue(this), diskCluster(nullptr) {
    RASSERT0(pollerCount > 0);
    stats = new EventScopeStats(this, nullptr);
    mainCluster = new Cluster(*this, pollerCount, _friend<EventScope>());   // create main cluster
  }

  void initSync() {
    struct rlimit rl;
    SYSCALL(getrlimit(RLIMIT_NOFILE, &rl));                                 // get hard limit for file descriptor count
    rl.rlim_max = rl.rlim_cur;                                              // firm up current FD limit
    SYSCALL(setrlimit(RLIMIT_NOFILE, &rl));                                 // and install maximum
    fdCount = rl.rlim_max + MasterPoller::extraTimerFD;                     // add fake timer fd, if necessary
    fdSyncVector = new SyncFD[fdCount];                                     // create vector of R/W sync points
  }

  void start() {
    masterPoller = new MasterPoller(*this, fdCount, _friend<EventScope>()); // start master poller & timer handling
    mainCluster->startPolling(_friend<EventScope>());                       // start polling now (potentially new event scope)
  }

public:
  EventScopeStats* stats;

  /** Create an event scope during bootstrap. */
  static EventScope* bootstrap(size_t pollerCount = 1, size_t workerCount = 1) {
    EventScope* es = new EventScope(pollerCount);
    es->mainFibre = es->mainCluster->registerWorker(_friend<EventScope>());
    if (workerCount > 1) es->mainCluster->addWorkers(workerCount - 1);
    es->initSync();
    es->start();
    return es;
  }

  /** Create a event scope by cloning the current one.
      The new event scope automatically starts with a single worker (pthread)
      and a separate kernel file descriptor table where supported (Linux).
      `mainFunc(mainArg)` is invoked as main fibre of the new scope. */
  EventScope* clone(funcvoid1_t mainFunc, ptr_t mainArg, size_t pollerCount = 1) {
    EventScope* es = new EventScope(pollerCount, this);
    es->mainCluster->addWorker((funcvoid1_t)cloneInternal, (ptr_t)es); // calls initSync()/start()
    es->mainFibre = new Fibre(*es->mainCluster);
    es->mainFibre->run(mainFunc, mainArg);
    return es;
  }

  void preFork() {
    // TODO: assert globalClusterCount == 1
    // TODO: test for other fibres?
    RASSERT0(CurrFibre() == mainFibre);
    RASSERT0(timerQueue.empty());
    RASSERT0(diskCluster == nullptr);
    mainCluster->preFork(_friend<EventScope>());
  }

  void postFork() {
    new (stats) EventScopeStats(this, nullptr);
    timerQueue.reinit(this);
    delete masterPoller;
    masterPoller = new MasterPoller(*this, fdCount, _friend<EventScope>()); // start master poller & timer handling
    mainCluster->postFork1(this, _friend<EventScope>());
    for (int f = 0; f < fdCount; f += 1) {
      RASSERT(fdSyncVector[f].iSem.getValue() >= 0, f);
      RASSERT(fdSyncVector[f].oSem.getValue() >= 0, f);
    }
    mainCluster->postFork2(_friend<EventScope>());
  }

  /** Wait for the main routine of a cloned event scope. */
  void join() { mainFibre->join(); }

  /** Create disk cluster (if needed for application). */
  Cluster& addDiskCluster(size_t cnt = 1) {
    RASSERT0(!diskCluster);
    diskCluster = new Cluster;
    diskCluster->addWorkers(cnt);
    return *diskCluster;
  }

  /** Set event-scope-local data. */
  void setClientData(void* cd) { clientData = cd; }

  /** Get event-scope-local data. */
  void* getClientData() { return clientData; }

  TimerQueue& getTimerQueue() { return timerQueue; }

  void setTimer(const Time& timeout) { masterPoller->setTimer(timeout); }

  void setBlocking(int fd, bool nonblocking) {
    RASSERT0(fd >= 0 && fd < fdCount);
    fdSyncVector[fd].blocking = !nonblocking;
  }

  void dupBlocking(int fd, int orig) {
    RASSERT0(fd >= 0 && fd < fdCount);
    fdSyncVector[fd].blocking = fdSyncVector[orig].blocking;
  }

  void cleanupFD(int fd) {
    RASSERT0(fd >= 0 && fd < fdCount);
    SyncFD& fdsync = fdSyncVector[fd];
    fdsync.iSem.reset(0);
    fdsync.oSem.reset(0);
    fdsync.iPoller = nullptr;
    fdsync.oPoller = nullptr;
    fdsync.blocking = false;
  }

  bool tryblock(int fd) {
    RASSERT0(fd >= 0 && fd < fdCount);
    return fdSyncVector[fd].iSem.tryP();
  }

  template<bool Input, bool Enqueue = true>
  Fred* unblock(int fd, _friend<BasePoller>) {
    RASSERT0(fd >= 0 && fd < fdCount);
    SyncSem& sem = Input ? fdSyncVector[fd].iSem : fdSyncVector[fd].oSem;
    return sem.V<Enqueue>();
  }

  void registerPollFD(int fd) {
    RASSERT0(fd >= 0 && fd < fdCount);
    masterPoller->setupFD(fd, Poller::Create, Poller::Input, Poller::Oneshot);
  }

  void blockPollFD(int fd) {
    RASSERT0(fd >= 0 && fd < fdCount);
    masterPoller->setupFD(fd, Poller::Modify, Poller::Input, Poller::Oneshot);
    fdSyncVector[fd].iSem.P();
  }

  void unblockPollFD(int fd, _friend<PollerFibre>) {
    RASSERT0(fd >= 0 && fd < fdCount);
    fdSyncVector[fd].iSem.V();
  }

  template<typename T, class... Args>
  T directIO(T (*diskfunc)(Args...), Args... a) {
    RASSERT0(diskCluster);
    BaseProcessor& proc = Fibre::migrateNow(*diskCluster, _friend<EventScope>());
    int result = diskfunc(a...);
    Fibre::migrateNow(proc, _friend<EventScope>());
    return result;
  }

  template<bool Input>
  static inline bool TestEAGAIN() {
    int serrno = _SysErrno();
    Context::CurrEventScope().stats->resets.count((int)(serrno == ECONNRESET));
#if __FreeBSD__
    // workaround - suspect: https://bugs.freebsd.org/bugzilla/show_bug.cgi?id=129169 - or similar?
    return serrno == EAGAIN || (Input == false && serrno == ENOTCONN);
#else // __linux__
    return serrno == EAGAIN;
#endif
  }

  template<bool Input, bool Cluster>
  BasePoller& getPoller(int fd) {
    if (Input) {
#if TESTING_WORKER_POLLER
      if (!Cluster) return Cluster::getWorkerPoller();
#endif
      return Context::CurrCluster().getInputPoller(fd);
    }
    return Context::CurrCluster().getOutputPoller(fd);
  }

  template<bool Input, bool Yield, bool Cluster, typename T, class... Args>
  T syncIO( T (*iofunc)(int, Args...), int fd, Args... a) {
    RASSERT0(fd >= 0 && fd < fdCount);
    SyncFD& fdsync = fdSyncVector[fd];
    if (!fdsync.blocking) return iofunc(fd, a...);
    if (Yield) Fibre::yield();
    stats->calls.count();
    T ret = iofunc(fd, a...);
    if (ret >= 0 || !TestEAGAIN<Input>()) return ret;
    stats->fails.count();
    BasePoller* volatile& poller = Input ? fdsync.iPoller : fdsync.oPoller;
    if (!poller) {
      poller = &getPoller<Input,Cluster>(fd);
#if TESTING_ONESHOT_REGISTRATION
      poller->setupFD(fd, Poller::Create, Input ? Poller::Input : Poller::Output, Poller::Oneshot);
#else
      poller->setupFD(fd, Poller::Create, Input ? Poller::Input : Poller::Output, Poller::Edge);
#endif
    } else {
#if TESTING_ONESHOT_REGISTRATION
      poller->setupFD(fd, Poller::Modify, Input ? Poller::Input : Poller::Output, Poller::Oneshot);
#endif
    }
    SyncSem& sem = Input ? fdsync.iSem : fdsync.oSem;
    for (;;) {
      sem.P();
      stats->calls.count();
      ret = iofunc(fd, a...);
      if (ret >= 0 || !TestEAGAIN<Input>()) return ret;
      stats->fails.count();
#if TESTING_ONESHOT_REGISTRATION
      poller->setupFD(fd, Poller::Modify, Input ? Poller::Input : Poller::Output, Poller::Oneshot);
#endif
    }
  }

  int checkAsyncCompletion(int fd) {
    RASSERT0(fd >= 0 && fd < fdCount);
    SyncFD& fdsync = fdSyncVector[fd];
    fdsync.oPoller = &getPoller<false,false>(fd);
    fdsync.oPoller->setupFD(fd, Poller::Create, Poller::Output, Poller::Oneshot); // register immediately
    fdsync.oSem.P();                                                              // wait for completion
    int err;
    socklen_t sz = sizeof(err);
    SYSCALL(getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &sz));
    return err;
  }
};

/** @brief Generic input wrapper. User-level-block if file descriptor not ready for reading. */
template<typename T, class... Args>
inline T lfInput( T (*readfunc)(int, Args...), int fd, Args... a) {
  return Context::CurrEventScope().syncIO<true,true,false>(readfunc, fd, a...); // yield before read
}

/** @brief Generic output wrapper. User-level-block if file descriptor not ready for writing. */
template<typename T, class... Args>
inline T lfOutput( T (*writefunc)(int, Args...), int fd, Args... a) {
  return Context::CurrEventScope().syncIO<false,false,false>(writefunc, fd, a...); // no yield before write
}

/** @brief Generic wrapper for I/O that cannot be polled. Fibre is migrated to disk cluster for execution. */
template<typename T, class... Args>
inline T lfDirectIO( T (*diskfunc)(int, Args...), int fd, Args... a) {
  return Context::CurrEventScope().directIO(diskfunc, fd, a...);
}

/** @brief Create new socket. */
inline int lfSocket(int domain, int type, int protocol) {
  int ret = socket(domain, type | SOCK_NONBLOCK, protocol);
  if (ret < 0) return ret;
  Context::CurrEventScope().setBlocking(ret, type & SOCK_NONBLOCK);
  return ret;
}

/** @brief Bind socket to local name. */
inline int lfBind(int fd, const sockaddr *addr, socklen_t addrlen) {
  int ret = bind(fd, addr, addrlen);
  if (ret < 0) {
    if (_SysErrno() != EINPROGRESS) return ret;
    ret = Context::CurrEventScope().checkAsyncCompletion(fd);
    if (ret != 0) {
      _SysErrnoSet() = ret;
      return -1;
    }
  }
  return ret;
}

/** @brief Create new connection. */
inline int lfConnect(int fd, const sockaddr *addr, socklen_t addrlen) {
  int ret = connect(fd, addr, addrlen);
  if (ret < 0) {
    if (_SysErrno() != EINPROGRESS) return ret;
    ret = Context::CurrEventScope().checkAsyncCompletion(fd);
    if (ret != 0) {
      _SysErrnoSet() = ret;
      return -1;
    }
  }
  Context::CurrEventScope().stats->cliconn.count();
  return ret;
}

/** @brief Set up socket listen queue. */
inline int lfListen(int fd, int backlog) {
  return listen(fd, backlog);
}

/** @brief Accept new connection. New file descriptor registered for I/O events. */
inline int lfAccept(int fd, sockaddr *addr, socklen_t *addrlen, int flags = 0) {
  int ret = Context::CurrEventScope().syncIO<true,false,true>(accept4, fd, addr, addrlen, flags | SOCK_NONBLOCK);
  if (ret < 0) return ret;
  Context::CurrEventScope().setBlocking(ret, flags & SOCK_NONBLOCK);
  Context::CurrEventScope().stats->srvconn.count();
  return ret;
}

/** @brief Nonblocking accept for listen queue draining. New file descriptor registered for I/O events. */
inline int lfTryAccept(int fd, sockaddr *addr, socklen_t *addrlen, int flags = 0) {
  int ret = accept4(fd, addr, addrlen, flags | SOCK_NONBLOCK);
  if (ret < 0) return ret;
  Context::CurrEventScope().setBlocking(ret, flags & SOCK_NONBLOCK);
  Context::CurrEventScope().stats->srvconn.count();
  return ret;
}

// not necessarily a good idea (on Linux?)
/** @brief Clone file descriptor. */
inline int lfDup(int fd) {
  int ret = dup(fd);
  if (ret < 0) return ret;
  Context::CurrEventScope().dupBlocking(ret, fd);
  return ret;
}

/** @brief Create pipe. */
inline int lfPipe(int pipefd[2], int flags = 0) {
  int ret = pipe2(pipefd, flags | O_NONBLOCK);
  if (ret < 0) return ret;
  Context::CurrEventScope().setBlocking(pipefd[0], flags & O_NONBLOCK);
  Context::CurrEventScope().setBlocking(pipefd[1], flags & O_NONBLOCK);
  return ret;
}

/** @brief Set file descriptor flags. */
inline int lfFcntl(int fd, int cmd, int flags) {
  int ret = ::fcntl(fd, cmd, flags | O_NONBLOCK); // internally, all sockets nonblocking
  if (ret < 0) return ret;
  Context::CurrEventScope().setBlocking(fd, flags & O_NONBLOCK);
  return ret;
}

/** @brief Close file descriptor. */
inline int lfClose(int fd) {
 Context::CurrEventScope().cleanupFD(fd);
  return close(fd);
}

#endif /* _EventScope_h_ */
