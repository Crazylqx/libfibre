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
#ifndef _EventScope_h_
#define _EventScope_h_ 1

/** @file */

#include "libfibre/Fibre.h"
#include "libfibre/Cluster.h"
#include "libfibre/OsProcessor.h"

#include <unistd.h>       // close
#include <sys/resource.h> // getrlimit
#include <sys/types.h>
#include <sys/socket.h>

// A vector for FDs works well here in principle, because POSIX guarantees lowest-numbered FDs:
// http://pubs.opengroup.org/onlinepubs/9699919799/functions/V2_chap02.html#tag_15_14
// A fixed-size array based on 'getrlimit' is somewhat brute-force, but simple and fast.

/**
 An EventScope object holds a set of Clusters and provides a common I/O
 scope.  Multiple EventScope objects can be used to take advantage of
 partitioned kernel file descriptor tables on Linux.
*/
class EventScope {
  struct SyncSem {
    TaskLock            mtx;
    TaskBinarySemaphore sem;
  };
  struct SyncFD {
    SyncSem RD;
    SyncSem WR;
#if TESTING_LAZY_FD_REGISTRATION
    TaskLock lock;
    size_t     status;
    SyncFD() : status(0) {}
#endif
  } *fdSyncVector;

  int fdCount;

  MasterPoller* masterPoller; // runs without cluster
  TimerQueue    timerQueue;   // scope-global timer queue

  // on Linux, file I/O cannot be monitored via select/poll/epoll
  // therefore, all file operations are executed on dedicated processor(s)
  Cluster*      diskCluster;

  // main cluster, processor. fibre
  Cluster*      mainCluster;
  OsProcessor*  mainProcessor;

  // simple kludge to provide event-scope-local data
  void*         clientData;

  // initialization happens after new scope is created with pthread_create() and unshare()
  void init() {
    stats = new ConnectionStats(this);
    struct rlimit rl;
    SYSCALL(getrlimit(RLIMIT_NOFILE, &rl));           // get hard limit for file descriptor count
    fdCount = rl.rlim_max + MasterPoller::extraTimerFD;
    fdSyncVector = new SyncFD[fdCount];               // create vector of R/W sync points
    masterPoller = new MasterPoller(*this, fdCount - 1, _friend<EventScope>()); // start master poller & timer handling
    mainCluster->startPolling(_friend<EventScope>()); // start main cluster's poller
  }

  static void split(void* This) {
#if __linux__
    SYSCALL(unshare(CLONE_FILES));
#endif
    reinterpret_cast<EventScope*>(This)->init();
  }

public:
  ConnectionStats* stats;

  /** Constructor. */
  EventScope(size_t pollerCount = 1, void* cd = nullptr) : diskCluster(nullptr), clientData(cd) {
    mainCluster = new Cluster(*this, pollerCount, _friend<EventScope>()); // delayed master poller start
    mainProcessor = new OsProcessor(*mainCluster, split, this, _friend<EventScope>()); // waits until phread running and split() called
  }
  EventScope(_friend<_Bootstrapper> fb, size_t pollerCount = 1) : diskCluster(nullptr), clientData(nullptr) {
    mainCluster = new Cluster(*this, pollerCount, _friend<EventScope>()); // delayed master poller start
    mainProcessor = new OsProcessor(*mainCluster, fb);
    init(); // bootstrap event scope -> no unshare() necessary
  }
  ~EventScope() {
    delete mainProcessor;
    delete mainCluster;
    delete masterPoller;
    delete[] fdSyncVector;
  }

  /** Create disk cluster (if needed for application). */
  Cluster& addDiskCluster() {
    RASSERT0(!diskCluster);
    diskCluster = new Cluster;
    return *diskCluster;
  }

  /** Obtain reference to main cluster - needed to bootstrap fibres in new event scope. */
  Cluster& getMainCluster() { return *mainCluster; }

  void setClientData(void* cd) { clientData = cd; }
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

  template<bool Input, bool Output, bool Lazy, bool Cluster>
  bool registerFD(int fd) {
    static_assert(Input || Output, "must set Input or Output in registerFD()");
    const size_t target = (Input ? BasePoller::Input : 0) | (Output ? BasePoller::Output : 0);

#if TESTING_LAZY_FD_REGISTRATION
    if (Lazy) return false;
#endif

#if TESTING_LAZY_FD_REGISTRATION
    RASSERT0(fd >= 0 && fd < fdCount);
    SyncFD& fdsync = fdSyncVector[fd];
    if ((fdsync.status & target) == target) return false; // outside of lock: faster, but double regs possible...
    ScopedLock<TaskLock> sl(fdsync.lock);
    bool change = fdsync.status;                          // already registered for polling?
    fdsync.status |= target;
#endif

#if TESTING_PROCESSOR_POLLER
    BasePoller& cp = Cluster ? Context::CurrCluster().getPoller(fd) : Context::CurrPoller();
#else
    BasePoller& cp = Context::CurrCluster().getPoller(fd);
#endif

#if TESTING_LAZY_FD_REGISTRATION
    cp.setupFD(fd, fdsync.status, change);                // add or modify poll settings
#else
    cp.setupFD(fd, target, false);
#endif
    return true;
  }

  template<bool RemoveFromPollSet = false>
  void deregisterFD(int fd) {
    RASSERT0(fd >= 0 && fd < fdCount);
    SyncFD& fdsync = fdSyncVector[fd];
    RASSERT0(fdsync.RD.sem.empty());
    RASSERT0(fdsync.WR.sem.empty());
#if TESTING_LAZY_FD_REGISTRATION
    ScopedLock<TaskLock> sl(fdsync.lock);
    fdsync.status = 0;
#endif
    if (RemoveFromPollSet) {                        // only called from lfConnect
#if TESTING_PROCESSOR_POLLER
      BasePoller& cp = Context::CurrPoller();
#else
      BasePoller& cp = Context::CurrCluster().getPoller(fd);
#endif
      cp.resetFD(fd);
    }
  }

  void registerPollFD(int fd) {
    RASSERT0(fd >= 0 && fd < fdCount);
    masterPoller->setupPollFD(fd, false); // set using ONESHOT to reduce polling
  }

  void blockPollFD(int fd) {
    RASSERT0(fd >= 0 && fd < fdCount);
    masterPoller->setupPollFD(fd, true);  // reset using ONESHOT to reduce polling
    ScopedLock<TaskLock> sl(fdSyncVector[fd].RD.mtx);
    fdSyncVector[fd].RD.sem.P();
  }

  void unblockPollFD(int fd, _friend<PollerFibre>) {
    RASSERT0(fd >= 0 && fd < fdCount);
    fdSyncVector[fd].RD.sem.V();
  }

  void suspendFD(int fd) {
    RASSERT0(fd >= 0 && fd < fdCount);
    fdSyncVector[fd].RD.sem.P_fake();
    fdSyncVector[fd].WR.sem.P_fake();
  }

  void resumeFD(int fd) {
    RASSERT0(fd >= 0 && fd < fdCount);
    fdSyncVector[fd].RD.sem.V();
    fdSyncVector[fd].WR.sem.V();
  }

  template<bool Input>
  void block(int fd) {
    RASSERT0(fd >= 0 && fd < fdCount);
    SyncSem& sync = Input ? fdSyncVector[fd].RD : fdSyncVector[fd].WR;
    ScopedLock<TaskLock> sl(sync.mtx);
    sync.sem.P();
  }

  template<bool Input>
  bool tryblock(int fd) {
    RASSERT0(fd >= 0 && fd < fdCount);
    SyncSem& sync = Input ? fdSyncVector[fd].RD : fdSyncVector[fd].WR;
    return sync.sem.tryP();
  }

  template<bool Input, bool Enqueue = true>
  StackContext* unblock(int fd, _friend<BasePoller>) {
    RASSERT0(fd >= 0 && fd < fdCount);
    SyncSem& sync = Input ? fdSyncVector[fd].RD : fdSyncVector[fd].WR;
    return sync.sem.V<Enqueue>();
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

  template<bool Input, bool Yield, typename T, class... Args>
  T syncIO( T (*iofunc)(int, Args...), int fd, Args... a) {
    RASSERT0(fd >= 0 && fd < fdCount);
    if (Yield) Fibre::yield();
    T ret = iofunc(fd, a...);
    if (ret >= 0 || !TestEAGAIN<Input>()) return ret;
#if TESTING_LAZY_FD_REGISTRATION
    if (registerFD<Input,!Input,false,false>(fd)) {
      Fibre::yield();
      T ret = iofunc(fd, a...);
      if (ret >= 0 || !TestEAGAIN<Input>()) return ret;
    }
#endif
    SyncSem& sync = Input ? fdSyncVector[fd].RD : fdSyncVector[fd].WR;
    ScopedLock<TaskLock> sl(sync.mtx);
    for (;;) {
      sync.sem.P();
      ret = iofunc(fd, a...);
      if (ret >= 0 || !TestEAGAIN<Input>()) break;
    }
    return ret;
  }
};

/** @brief Generic input wrapper. User-level-block if file descriptor not ready for reading. */
template<typename T, class... Args>
inline T lfInput( T (*readfunc)(int, Args...), int fd, Args... a) {
  return Context::CurrEventScope().syncIO<true,true>(readfunc, fd, a...); // yield before read
}

/** @brief Generic output wrapper. User-level-block if file descriptor not ready for writing. */
template<typename T, class... Args>
inline T lfOutput( T (*writefunc)(int, Args...), int fd, Args... a) {
  return Context::CurrEventScope().syncIO<false,false>(writefunc, fd, a...); // no yield before write
}

/** @brief Generic wrapper for I/O that cannot be polled. Fibre is migrated to disk cluster for execution. */
template<typename T, class... Args>
inline T lfDirectIO( T (*diskfunc)(int, Args...), int fd, Args... a) {
  return Context::CurrEventScope().directIO(diskfunc, fd, a...);
}

/** @brief Create new socket. */
inline int lfSocket(int domain, int type, int protocol) {
  int ret = socket(domain, type | SOCK_NONBLOCK, protocol);
  // do not register SOCK_STREAM yet (cf. listen, connect) -> mandatory for FreeBSD!
  if (ret >= 0) if (type != SOCK_STREAM) Context::CurrEventScope().registerFD<true,true,true,false>(ret);
  return ret;
}

/** @brief Bind socket to local name. */
inline int lfBind(int fd, const sockaddr *addr, socklen_t addrlen) {
  int ret = bind(fd, addr, addrlen);
  if (ret < 0 && _SysErrno() != EINPROGRESS) return ret;
  return 0;
}

/** @brief Set up socket listen queue. */
inline int lfListen(int fd, int backlog) {
  int ret = listen(fd, backlog);
  if (ret < 0) return ret;
  // register SOCK_STREAM server fd only after 'listen' system call (cf. socket/connect)
  Context::CurrEventScope().registerFD<true,false,false,true>(fd);
  return 0;
}

/** @brief Accept new connection. New file descriptor registered for I/O events. */
inline int lfAccept(int fd, sockaddr *addr, socklen_t *addrlen, int flags = 0) {
  int ret = Context::CurrEventScope().syncIO<true,false>(accept4, fd, addr, addrlen, flags | SOCK_NONBLOCK);
  if (ret >= 0) {
    Context::CurrEventScope().stats->srvconn.count();
    Context::CurrEventScope().registerFD<true,true,true,false>(ret);
  }
  return ret;
}

/** @brief Nonblocking accept for listen queue draining. New file descriptor registered for I/O events. */
inline int lfTryAccept(int fd, sockaddr *addr, socklen_t *addrlen, int flags = 0) {
  int ret = accept4(fd, addr, addrlen, flags | SOCK_NONBLOCK);
  if (ret >= 0) {
    Context::CurrEventScope().stats->srvconn.count();
    Context::CurrEventScope().registerFD<true,true,true,false>(ret);
  }
  return ret;
}

/** @brief Create new connection. */
inline int lfConnect(int fd, const sockaddr *addr, socklen_t addrlen) {
  int ret = connect(fd, addr, addrlen);
  if (ret >= 0) {
    Context::CurrEventScope().registerFD<true,true,true,false>(fd);  // register lazily
    Context::CurrEventScope().stats->cliconn.count();
  } else if (_SysErrno() == EINPROGRESS) {
    Context::CurrEventScope().registerFD<true,true,false,false>(fd); // register immediately
    Context::CurrEventScope().block<false>(fd);       // wait for connect to complete
#if TESTING_LAZY_FD_REGISTRATION
    Context::CurrEventScope().deregisterFD<true>(fd); // revert to lazy registration
#endif
    socklen_t sz = sizeof(ret);
    SYSCALL(getsockopt(fd, SOL_SOCKET, SO_ERROR, &ret, &sz));
    RASSERT(ret == 0, ret);
    Context::CurrEventScope().stats->cliconn.count();
  }
  return ret;
}

// not necessarily a good idea (on Linux?)
/** @brief Clone file descriptor. */
inline int lfDup(int fd) {
  int ret = dup(fd);
  if (ret >= 0) Context::CurrEventScope().registerFD<true,true,true,false>(ret);
  return ret;
}

/** @brief Close file descriptor. */
inline int lfClose(int fd) {
  Context::CurrEventScope().deregisterFD(fd);
  return close(fd);
}

#endif /* _EventScope_h_ */
