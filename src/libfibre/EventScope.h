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
  typedef Mutex<FastMutex>                 SyncMutex;
  typedef LockedSemaphore<WorkerLock,true> SyncSem;
  struct SyncFD {
    SyncSem   rdSem;
    SyncSem   wrSem;
#if TESTING_ONESHOT_REGISTRATION && defined(__linux__)
    SyncMutex rwMutex;
    bool      pollMod;
#else
    SyncMutex rdMutex;
    SyncMutex wrMutex;
#endif
    bool nonblocking;
#if TESTING_LAZY_FD_REGISTRATION
    FastMutex   pollStatusLock;
    size_t      pollStatus;
    BasePoller* poller;
#endif
    SyncFD() : nonblocking(false) {
#if TESTING_ONESHOT_REGISTRATION && defined(__linux__)
      pollMod = false;
#endif
#if TESTING_LAZY_FD_REGISTRATION
      pollStatus = 0;
      poller = nullptr;
#endif
    }
  } *fdSyncVector;

  int fdCount;

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
#if __linux__
    SYSCALL(unshare(CLONE_FILES));
#endif
    This->initIO();
  }

  EventScope(size_t pollerCount) : diskCluster(nullptr) {
    RASSERT0(pollerCount > 0);
    stats = new IOStats(this);
    mainCluster = new Cluster(*this, pollerCount, _friend<EventScope>());   // create main cluster
  }

  void initIO() {
    struct rlimit rl;
    SYSCALL(getrlimit(RLIMIT_NOFILE, &rl));                                 // get hard limit for file descriptor count
    rl.rlim_max = rl.rlim_cur;                                              // firm up current FD limit
    SYSCALL(setrlimit(RLIMIT_NOFILE, &rl));                                 // and install maximum
    fdCount = rl.rlim_max + MasterPoller::extraTimerFD;                     // add fake timer fd, if necessary
    fdSyncVector = new SyncFD[fdCount];                                     // create vector of R/W sync points
    masterPoller = new MasterPoller(*this, fdCount, _friend<EventScope>()); // start master poller & timer handling
    mainCluster->startPolling(_friend<EventScope>());                       // start polling now (potentially new event scope)
  }

public:
  IOStats* stats;

  /** Create an event scope during bootstrap. */
  static EventScope* bootstrap(size_t pollerCount = 1, size_t workerCount = 1) {
    EventScope* es = new EventScope(pollerCount);
    es->mainFibre = es->mainCluster->registerWorker(_friend<EventScope>());
    if (workerCount > 1) es->mainCluster->addWorkers(workerCount - 1);
    es->initIO();
    return es;
  }

  /** Create a event scope by cloning the current one.
      The new event scope automatically starts with a single worker (pthread)
      and a separate kernel file descriptor table where supported (Linux).
      `mainFunc(mainArg)` is invoked as main fibre of the new scope. */
  static EventScope* clone(funcvoid1_t mainFunc, ptr_t mainArg, size_t pollerCount = 1) {
    EventScope* es = new EventScope(pollerCount);
    es->mainCluster->addWorker((funcvoid1_t)cloneInternal, (ptr_t)es); // calls initIO()
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
    new (stats) IOStats(this);
    timerQueue.reinit();
    delete masterPoller;
    masterPoller = new MasterPoller(*this, fdCount, _friend<EventScope>()); // start master poller & timer handling
    mainCluster->postFork1(_friend<EventScope>());
    for (int f = 0; f < fdCount; f += 1) {
      RASSERT(fdSyncVector[f].rdSem.getValue() >= 0, f);
      RASSERT(fdSyncVector[f].wrSem.getValue() >= 0, f);
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

  void setTimer(const Time& timeout) {
    masterPoller->setTimer(timeout);
  }

  TimerQueue& getTimerQueue() {
    return timerQueue;
  }

  void checkTimers(const Time& currTime) {
    Time newTime;
    if (timerQueue.checkExpiry(currTime, newTime)) setTimer(newTime);
  }

private:
  template<bool Input, bool Output, bool Cluster>
  bool internalRegisterFD(int fd, bool now) {
    static_assert(Input || Output, "must set Input or Output in internalRegisterFD()");
    const size_t target = (Input ? BasePoller::Input : 0) | (Output ? BasePoller::Output : 0);

#if TESTING_LAZY_FD_REGISTRATION
    if (!now) return false;
    RASSERT0(fd >= 0 && fd < fdCount);
    SyncFD& fdsync = fdSyncVector[fd];
    if ((fdsync.pollStatus & target) == target) return false; // outside of lock: faster, but double regs possible...
    ScopedLock<FastMutex> sl(fdsync.pollStatusLock);
    RASSERT0((bool)fdsync.pollStatus == (bool)fdsync.poller)
    fdsync.pollStatus |= target;
#endif

#if TESTING_PROCESSOR_POLLER
    BasePoller& cp = Cluster
      ? static_cast<BasePoller&>(Context::CurrCluster().getPoller(fd))
      : static_cast<BasePoller&>(Context::CurrPoller());
#else
    BasePoller& cp = Context::CurrCluster().getPoller(fd);
#endif

#if TESTING_LAZY_FD_REGISTRATION
    if (fdsync.poller) {
      fdsync.poller->setupFD(fd, fdsync.pollStatus, true); // modify poll settings
    } else {
      fdsync.poller = &cp;
      fdsync.poller->setupFD(fd, fdsync.pollStatus);       // add poll settings
    }
#elif TESTING_ONESHOT_REGISTRATION && defined(__linux__)
    cp.setupFD(fd, target, fdSyncVector[fd].pollMod);      // add poll settings
    fdSyncVector[fd].pollMod = true;
#else
    cp.setupFD(fd, target);                                // add poll settings
#endif
    return true;
  }

public:
  bool registerServerFD(int fd, bool now = false) {
    return internalRegisterFD<true,false,true>(fd, now);
  }

  bool registerFD(int fd, bool now = false) {
    return internalRegisterFD<true,true,false>(fd, now);
  }

  template<bool RemoveFromPollSet = false>
  void deregisterFD(int fd) {
    RASSERT0(fd >= 0 && fd < fdCount);
    SyncFD& fdsync = fdSyncVector[fd];
    fdsync.rdSem.cleanup();
    fdsync.wrSem.cleanup();
    fdsync.nonblocking = false;
#if TESTING_ONESHOT_REGISTRATION && defined(__linux__)
    fdsync.pollMod = false;
#endif
#if TESTING_LAZY_FD_REGISTRATION
    ScopedLock<FastMutex> sl(fdsync.pollStatusLock);
    fdsync.pollStatus = 0;
    if (RemoveFromPollSet) {              // only called from lfConnect w/ TESTING_LAZY_FD_REGISTRATION
      RASSERT0(fdsync.poller)
      fdsync.poller->resetFD(fd);
    }
    fdsync.poller = nullptr;
#endif
  }

  void checkAsyncCompletion(int fd) {
    RASSERT0(fd >= 0 && fd < fdCount);
    registerFD(fd, true);                 // register immediately
    fdSyncVector[fd].wrSem.P();           // wait for completion
    int ret;
    socklen_t sz = sizeof(ret);
    SYSCALL(getsockopt(fd, SOL_SOCKET, SO_ERROR, &ret, &sz));
    RASSERT(ret == 0, ret);
#if TESTING_LAZY_FD_REGISTRATION
    deregisterFD<true>(fd);               // revert to lazy registration
#endif
  }

  void registerPollFD(int fd) {
    RASSERT0(fd >= 0 && fd < fdCount);
    masterPoller->setupPollFD(fd, false); // set using ONESHOT to reduce polling
  }

  void blockPollFD(int fd) {
    RASSERT0(fd >= 0 && fd < fdCount);
    masterPoller->setupPollFD(fd, true);  // reset using ONESHOT to reduce polling
    fdSyncVector[fd].rdSem.P();
  }

  void unblockPollFD(int fd, _friend<PollerFibre>) {
    RASSERT0(fd >= 0 && fd < fdCount);
    fdSyncVector[fd].rdSem.V();
  }

  bool tryblock(int fd) {
    RASSERT0(fd >= 0 && fd < fdCount);
    return fdSyncVector[fd].rdSem.tryP();
  }

  template<bool Input, bool Enqueue = true>
  Fred* unblock(int fd, _friend<BasePoller>) {
    RASSERT0(fd >= 0 && fd < fdCount);
    SyncSem& sem = Input ? fdSyncVector[fd].rdSem : fdSyncVector[fd].wrSem;
    return sem.V<Enqueue>();
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

  template<bool Input, bool Yield, bool Cluster, typename T, class... Args>
  T syncIO( T (*iofunc)(int, Args...), int fd, Args... a) {
    RASSERT0(fd >= 0 && fd < fdCount);
    if (fdSyncVector[fd].nonblocking) return iofunc(fd, a...);
    if (Yield) Fibre::yield();
    stats->calls.count();
    T ret = iofunc(fd, a...);
    if (ret >= 0 || !TestEAGAIN<Input>()) return ret;
    stats->fails.count();
#if TESTING_LAZY_FD_REGISTRATION
    if (internalRegisterFD<Input,!Input,Cluster>(fd, true)) {
      Fibre::yield();
      stats->calls.count();
      T ret = iofunc(fd, a...);
      if (ret >= 0 || !TestEAGAIN<Input>()) return ret;
      stats->fails.count();
    }
#endif
    SyncSem& sem = Input ? fdSyncVector[fd].rdSem : fdSyncVector[fd].wrSem;
#if TESTING_ONESHOT_REGISTRATION && defined(__linux__)
    ScopedLock<SyncMutex> sl(fdSyncVector[fd].rwMutex);
#else
    ScopedLock<SyncMutex> sl(Input ? fdSyncVector[fd].rdMutex : fdSyncVector[fd].wrMutex);
#endif
    for (;;) {
#if TESTING_ONESHOT_REGISTRATION
      internalRegisterFD<Input,!Input,Cluster>(fd, true);
#endif
      sem.P();
      stats->calls.count();
      ret = iofunc(fd, a...);
      if (ret >= 0 || !TestEAGAIN<Input>()) return ret;
      stats->fails.count();
    }
  }

  int fcntl(int fildes, int cmd, int flags) {
    int ret = ::fcntl(fildes, cmd, flags | O_NONBLOCK); // internally, all sockets nonblocking
    if (ret != -1) {
      if (flags & O_NONBLOCK) {
        fdSyncVector[fildes].nonblocking = true;
      } else {
        fdSyncVector[fildes].nonblocking = false;
      }
    }
    return ret;
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
  // do not register SOCK_STREAM yet (cf. listen, connect) -> mandatory for FreeBSD!
  if (type != SOCK_STREAM) Context::CurrEventScope().registerFD(ret);
  return ret;
}

/** @brief Bind socket to local name. */
inline int lfBind(int fd, const sockaddr *addr, socklen_t addrlen) {
  int ret = bind(fd, addr, addrlen);
  if (ret >= 0) {
    return ret;
  } else if (_SysErrno() == EINPROGRESS) {
    Context::CurrEventScope().checkAsyncCompletion(fd);
    return 0;
  }
  return ret;
}

/** @brief Create new connection. */
inline int lfConnect(int fd, const sockaddr *addr, socklen_t addrlen) {
  int ret = connect(fd, addr, addrlen);
  if (ret >= 0) {
    Context::CurrEventScope().registerFD(fd);
    Context::CurrEventScope().stats->cliconn.count();
    return ret;
  } else if (_SysErrno() == EINPROGRESS) {
    Context::CurrEventScope().checkAsyncCompletion(fd);
    Context::CurrEventScope().stats->cliconn.count();
    return 0;
  }
  return ret;
}

/** @brief Set up socket listen queue. */
inline int lfListen(int fd, int backlog) {
  int ret = listen(fd, backlog);
  if (ret < 0) return ret;
  // register SOCK_STREAM server fd only after 'listen' system call (cf. socket/connect)
  Context::CurrEventScope().registerServerFD(fd);
  return ret;
}

/** @brief Accept new connection. New file descriptor registered for I/O events. */
inline int lfAccept(int fd, sockaddr *addr, socklen_t *addrlen, int flags = 0) {
  int ret = Context::CurrEventScope().syncIO<true,false,true>(accept4, fd, addr, addrlen, flags | SOCK_NONBLOCK);
  if (ret < 0) return ret;
  Context::CurrEventScope().stats->srvconn.count();
  Context::CurrEventScope().registerFD(ret);
  return ret;
}

/** @brief Nonblocking accept for listen queue draining. New file descriptor registered for I/O events. */
inline int lfTryAccept(int fd, sockaddr *addr, socklen_t *addrlen, int flags = 0) {
  int ret = accept4(fd, addr, addrlen, flags | SOCK_NONBLOCK);
  if (ret < 0) return ret;
  Context::CurrEventScope().stats->srvconn.count();
  Context::CurrEventScope().registerFD(ret);
  return ret;
}

// not necessarily a good idea (on Linux?)
/** @brief Clone file descriptor. */
inline int lfDup(int fd) {
  int ret = dup(fd);
  if (ret < 0) return ret;
  Context::CurrEventScope().registerFD(ret);
  return ret;
}

/** @brief Create pipe. */
inline int lfPipe(int pipefd[2], int flags = 0) {
  int ret = pipe2(pipefd, flags | O_NONBLOCK);
  if (ret < 0) return ret;
  Context::CurrEventScope().registerFD(pipefd[0]);
  Context::CurrEventScope().registerFD(pipefd[1]);
  return ret;
}

inline int lfFcntl(int fildes, int cmd, int flags) {
  return Context::CurrEventScope().fcntl(fildes, cmd, flags);
}

/** @brief Close file descriptor. */
inline int lfClose(int fd) {
  Context::CurrEventScope().deregisterFD(fd);
  return close(fd);
}

#endif /* _EventScope_h_ */
