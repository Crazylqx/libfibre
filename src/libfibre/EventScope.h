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

#include "libfibre/Fibre.h"
#include "libfibre/OsProcessor.h"

#include <unistd.h>       // close
#include <sys/resource.h> // getrlimit
#include <sys/types.h>
#include <sys/socket.h>

// A vector for FDs works well here in principle, because POSIX guarantees lowest-numbered FDs:
// http://pubs.opengroup.org/onlinepubs/9699919799/functions/V2_chap02.html#tag_15_14
// A fixed-size array based on 'getrlimit' is somewhat brute-force, but simple and fast.
class EventScope {
  typedef FifoSemaphore<InternalLock,true> SyncSem;
  struct SyncRW {
    SyncSem RD;
    SyncSem WR;
#if TESTING_LAZY_FD_REGISTRATION && __linux__
    FibreMutex regLock;
#endif
    BasePoller* poller;
    size_t status;
    SyncRW() : poller(nullptr), status(0) {}
  } *fdSyncVector;

  int fdcount;

  MasterPoller* masterPoller; // runs without cluster
  TimerQueue    timerQueue;   // scope-global timer queue

  // on Linux, file I/O cannot be monitored via select/poll/epoll
  // therefore, all file operations are executed on dedicated processor(s)
  FibreCluster* diskCluster;

  // main cluster, processor. fibre
  FibreCluster* mainCluster;
  OsProcessor*  mainProcessor;

  // simple kludge to provides event-scope-local data
  void*         clientData;

  // initialization happens after new scope is created with pthread_create() and unshare()
  void init() {
    stats = new ConnectionStats(this);
    struct rlimit rl;
    SYSCALL(getrlimit(RLIMIT_NOFILE, &rl));          // get hard limit for file descriptor count
    fdcount = rl.rlim_max + MasterPoller::extraTimerFD;
    fdSyncVector = new SyncRW[fdcount];              // create vector of R/W sync points
    masterPoller = new MasterPoller(*this, fdcount - 1, _friend<EventScope>()); // start master poller & timer handling
    mainCluster->startPoller(_friend<EventScope>()); // start main cluster's poller
  }

public:
  ConnectionStats* stats;

  EventScope(size_t p = 1, void* cd = nullptr) : diskCluster(nullptr), clientData(cd) {
    mainCluster = new FibreCluster(*this, _friend<EventScope>(), p); // poller start delayed
    mainProcessor = new OsProcessor(*mainCluster, *this, _friend<EventScope>());
    // OsProcessor calls split(), which calls init()
    mainProcessor->waitUntilRunning(); // wait for new pthread running
  }
  EventScope(_friend<_Bootstrapper> fb, size_t p = 1) : diskCluster(nullptr), clientData(nullptr) {
    mainCluster = new FibreCluster(*this, _friend<EventScope>(), p); // poller start delayed);
    mainProcessor = new OsProcessor(*mainCluster, fb);
    init(); // bootstrap event scope -> no unshare() necessary
  }
  ~EventScope() {
    delete mainProcessor;
    delete mainCluster;
    delete masterPoller;
    delete[] fdSyncVector;
  }

  static void split(EventScope* This, _friend<OsProcessor>) {
#if __linux__
    SYSCALL(unshare(CLONE_FILES));
#endif
    This->init();
  }

  FibreCluster& addDiskCluster() {
    RASSERT0(!diskCluster);
    diskCluster = new FibreCluster;
    return *diskCluster;
  }

  FibreCluster& getMainCluster() { return *mainCluster; }

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
  void registerFD(int fd) {
    static_assert(Input || Output, "must set Input or Output in registerFD()");

#if TESTING_LAZY_FD_REGISTRATION
    if (Lazy) return;
#endif

    RASSERT0(fd >= 0 && fd < fdcount);
    SyncRW& fdsync = fdSyncVector[fd];
    const size_t target = (Input ? BasePoller::Input : 0) | (Output ? BasePoller::Output : 0);
#if TESTING_PROCESSOR_POLLER
    BasePoller& cp = Cluster ? CurrCluster().getPoller(fd) : CurrProcessor().getPoller();
#else
    BasePoller& cp = CurrCluster().getPoller(fd);
#endif

#if TESTING_LAZY_FD_REGISTRATION
#if __FreeBSD__ // can atomically check flags and set poller
    size_t prev = __atomic_fetch_or(&fdsync.status, target, __ATOMIC_RELAXED);
    if ((prev & target) == target) return;
    const bool change = false;
    BasePoller* pp = nullptr;
    __atomic_compare_exchange_n(&fdsync.poller, &pp, &cp, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
#else // Linux: serialize concurrent registrations - EPOLL_CTL_ADD vs. _MOD
    if ((fdsync.status & target) == target) return;   // outside of lock: faster, but double regs possible...
    ScopedLock<FibreMutex> sl(fdsync.regLock);
    fdsync.status |= target;
    bool change = fdsync.poller;                      // already registered for polling?
    if (!fdsync.poller) fdsync.poller = &cp;          // else set poller
#endif
#else // TESTING_LAZY_FD_REGISTRATION
    RASSERT0(!fdsync.poller);
    fdsync.status |= target;
    const bool change = false;
    fdsync.poller = &cp;
#endif

    fdsync.poller->setupFD(fd, fdsync.status, change); // add or modify poll settings
  }

  void deregisterFD(int fd) {
    RASSERT0(fd >= 0 && fd < fdcount);
    SyncRW& fdsync = fdSyncVector[fd];
#if TESTING_LAZY_FD_REGISTRATION && __linux__
    ScopedLock<FibreMutex> sl(fdsync.regLock);
#endif
//    if (fdsync.poller) fdsync.poller->resetFD(fd);
    fdsync.poller = nullptr;
    fdsync.status = 0;
    RASSERT0(fdsync.RD.empty());
    RASSERT0(fdsync.WR.empty());
  }

  void registerPollFD(int fd) {
    RASSERT0(fd >= 0 && fd < fdcount);
    masterPoller->setupPollFD(fd, false); // set using ONESHOT to reduce polling
  }

  void blockPollFD(int fd) {
    RASSERT0(fd >= 0 && fd < fdcount);
    masterPoller->setupPollFD(fd, true);  // reset using ONESHOT to reduce polling
    fdSyncVector[fd].RD.P();
  }

  void unblockPollFD(int fd, _friend<PollerFibre>) {
    RASSERT0(fd >= 0 && fd < fdcount);
    fdSyncVector[fd].RD.V();
  }

  void suspendFD(int fd) {
    RASSERT0(fd >= 0 && fd < fdcount);
    fdSyncVector[fd].RD.P_fake();
    fdSyncVector[fd].WR.P_fake();
  }

  void resumeFD(int fd) {
    RASSERT0(fd >= 0 && fd < fdcount);
    fdSyncVector[fd].RD.V();
    fdSyncVector[fd].WR.V();
  }

  template<bool Input>
  void block(int fd) {
    RASSERT0(fd >= 0 && fd < fdcount);
    SyncSem& sem = Input ? fdSyncVector[fd].RD : fdSyncVector[fd].WR;
    sem.P();
  }

  template<bool Input>
  bool tryblock(int fd) {
    RASSERT0(fd >= 0 && fd < fdcount);
    SyncSem& sem = Input ? fdSyncVector[fd].RD : fdSyncVector[fd].WR;
    return sem.tryP();
  }

  template<bool Input, bool Enqueue = true>
  StackContext* unblock(int fd, _friend<BasePoller>) {
    RASSERT0(fd >= 0 && fd < fdcount);
    SyncSem& sem = Input ? fdSyncVector[fd].RD : fdSyncVector[fd].WR;
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
  static inline bool NBtest() {
#if __FreeBSD__
    // workaround - suspect: https://bugs.freebsd.org/bugzilla/show_bug.cgi?id=129169 - or similar?
    int ferrno = lfErrno();
    return ferrno == EAGAIN || (Input == false && ferrno == ENOTCONN);
#else // __linux__
    return lfErrno() == EAGAIN;
#endif
  }

  template<bool Input, bool Yield, typename T, class... Args>
  T syncIO( T (*iofunc)(int, Args...), int fd, Args... a) {
    RASSERT(fd >= 0 && fd < fdcount, fd, fdcount);
    T ret;
    if (Yield) Fibre::yield();
    ret = iofunc(fd, a...);
    if (ret >= 0 || !NBtest<Input>()) return ret;
#if TESTING_LAZY_FD_REGISTRATION
    registerFD<Input,!Input,false,false>(fd);
#endif
    SyncSem& sem = Input ? fdSyncVector[fd].RD : fdSyncVector[fd].WR;
    for (;;) {
      sem.P();
      ret = iofunc(fd, a...);
      if (ret >= 0 || !NBtest<Input>()) return ret;
    }
  }
};

// input: yield before network read
template<typename T, class... Args>
T lfInput( T (*readfunc)(int, Args...), int fd, Args... a) {
  return CurrEventScope().syncIO<true,true>(readfunc, fd, a...);
}

// output: no yield before write
template<typename T, class... Args>
T lfOutput( T (*writefunc)(int, Args...), int fd, Args... a) {
  return CurrEventScope().syncIO<false,false>(writefunc, fd, a...);
}

// direct I/O
template<typename T, class... Args>
T lfDirectIO( T (*diskfunc)(int, Args...), int fd, Args... a) {
  return CurrEventScope().directIO(diskfunc, fd, a...);
}

// socket creation: do not register SOCK_STREAM yet (cf. listen, connect) -> mandatory for FreeBSD!
static inline int lfSocket(int domain, int type, int protocol) {
  int ret = socket(domain, type | SOCK_NONBLOCK, protocol);
  if (ret >= 0) if (type != SOCK_STREAM) CurrEventScope().registerFD<true,true,true,false>(ret);
  return ret;
}

// POSIX says that bind might fail with EINPROGRESS (but not on Linux/FreeBSD)
static inline int lfBind(int fd, const sockaddr *addr, socklen_t addrlen) {
  int ret = bind(fd, addr, addrlen);
  if (ret < 0 && lfErrno() != EINPROGRESS) return ret;
  return 0;
}

// register SOCK_STREAM server fd only after 'listen' system call (cf. socket/connect)
static inline int lfListen(int fd, int backlog) {
  int ret = listen(fd, backlog);
  if (ret < 0) return ret;
  CurrEventScope().registerFD<true,false,false,true>(fd);
  return 0;
}

// nonblocking accept for accept draining: register new file descriptor for I/O events
static inline int lfTryAccept(int fd, sockaddr *addr, socklen_t *addrlen, int flags = 0) {
  int ret = accept4(fd, addr, addrlen, flags | SOCK_NONBLOCK);
  if (ret >= 0) {
    CurrEventScope().stats->servconn.count();
    CurrEventScope().registerFD<true,true,true,false>(ret);
  }
  return ret;
}

// accept: register new file descriptor for I/O events, no yield before accept
static inline int lfAccept(int fd, sockaddr *addr, socklen_t *addrlen, int flags = 0) {
  int ret = CurrEventScope().syncIO<true,false>(accept4, fd, addr, addrlen, flags | SOCK_NONBLOCK);
  if (ret >= 0) {
    CurrEventScope().stats->servconn.count();
    CurrEventScope().registerFD<true,true,true,false>(ret);
  }
  return ret;
}

// see man 3 connect for EINPROGRESS; register SOCK_STREAM fd now (cf. socket/listen)
static inline int lfConnect(int fd, const sockaddr *addr, socklen_t addrlen) {
  int ret = connect(fd, addr, addrlen);
  if (ret >= 0) {
    CurrEventScope().stats->clientconn.count();
    CurrEventScope().registerFD<true,true,true,false>(fd);
  } else if (lfErrno() == EINPROGRESS) {
    CurrEventScope().registerFD<true,true,false,false>(fd);
    CurrEventScope().block<false>(fd);
    socklen_t sz = sizeof(ret);
    SYSCALL(getsockopt(fd, SOL_SOCKET, SO_ERROR, &ret, &sz));
    RASSERT(ret == 0, ret);
    CurrEventScope().stats->clientconn.count();
  }
  return ret;
}

// dup: duplicate file descriptor -> not necessarily a good idea (on Linux?) - think twice about it!
static inline int lfDup(int fd) {
  int ret = dup(fd);
  if (ret >= 0) CurrEventScope().registerFD<true,true,true,false>(ret);
  return ret;
}

static inline int lfClose(int fd) {
  CurrEventScope().deregisterFD(fd);
  return close(fd);
}

static inline void lfRegister(int fd) {
  CurrEventScope().registerFD<true,true,false,false>(fd);
}

// TODO: lfShutdown: need to handle R/W separately

#endif /* _EventScope_h_ */
