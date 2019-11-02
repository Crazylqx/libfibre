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
#ifndef _Poller_h_
#define _Poller_h_ 1

#include "runtime/Debug.h"
#include "runtime/Stats.h"

class BaseProcessor;

#include <unistd.h>      // close
#if __FreeBSD__
#include <sys/event.h>
#else // __linux__ below
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#endif

class EventScope;
class Fibre;
class FibreCluster;
class StackContext;

class BasePoller {
public:
#if __FreeBSD__
  static const size_t Input  = 0x1;
  static const size_t Output = 0x2;
#else // __linux__ below
  static const size_t Input  = EPOLLIN | EPOLLPRI | EPOLLRDHUP;
  static const size_t Output = EPOLLOUT;
#endif

protected:
#if __FreeBSD__
  typedef struct kevent EventType;
  typedef struct kevent WakeType;
#else // __linux__ below
  typedef epoll_event   EventType;
  typedef int           WakeType;
#endif

  static const int maxPoll = 1024;
  EventType     events[maxPoll];
  int           pollFD;
  WakeType      waker;

  EventScope&   eventScope;
  volatile bool pollTerminate;

  PollerStats* stats;

  template<bool Blocking>
  inline int doPoll();

  template<bool Enqueue = true>
  inline StackContext* notifyOne(EventType& ev);

  inline void notifyAll(int evcnt);

  void wakeUp() {
#if __FreeBSD__
    EV_SET(&waker, 0, EVFILT_USER, EV_ENABLE, NOTE_TRIGGER, 0, 0);
    SYSCALL(kevent(pollFD, &waker, 1, nullptr, 0, nullptr));
#else // __linux__ below
    uint64_t val = 1;
    val = SYSCALL_EQ(write(waker, &val, sizeof(val)), sizeof(val));
#endif
    DBG::outl(DBG::Polling, "Poller ", FmtHex(this), " woke ", pollFD, " via ", waker);
  }

public:
  BasePoller(EventScope& es, const char* n = "BasePoller") : eventScope(es), pollTerminate(false) {
    stats = new PollerStats(this, n);
#if __FreeBSD__
    pollFD = SYSCALLIO(kqueue());
    DBG::outl(DBG::Polling, "Poller ", FmtHex(this), " create ", pollFD);
    EV_SET(&waker, 0, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, 0);
    SYSCALL(kevent(pollFD, &waker, 1, nullptr, 0, nullptr));
#else // __linux__ below
    pollFD = SYSCALLIO(epoll_create1(EPOLL_CLOEXEC));
    waker = SYSCALLIO(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)); // binary semaphore semantics w/o EFD_SEMAPHORE
    DBG::outl(DBG::Polling, "Poller ", FmtHex(this), " create ", pollFD, " and ", waker);
    setupFD(waker, Input);
#endif
  }
  ~BasePoller() {
#if __linux__
    SYSCALL(close(waker));
#endif
    SYSCALL(close(pollFD));
  }

  void setupFD(int fd, size_t status, bool change = false) {
    DBG::outl(DBG::Polling, "Poller ", FmtHex(this), " register ", fd, " on ", pollFD, " for ", status);
#if __FreeBSD__
    struct kevent ev[2];
    int idx = 0;
    if (status & Input) {
      EV_SET(&ev[idx], fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, 0);
      idx += 1;
    }
    if (status & Output) {
      EV_SET(&ev[idx], fd, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, 0);
      idx += 1;
    }
    SYSCALL(kevent(pollFD, ev, idx, nullptr, 0, nullptr));
#else // __linux__ below
    epoll_event ev;
    ev.events = EPOLLET | status; // man 2 epoll_ctl: EPOLLERR, EPOLLHUP not needed
    ev.data.fd = fd;
    SYSCALL(epoll_ctl(pollFD, change ? EPOLL_CTL_MOD : EPOLL_CTL_ADD, fd, &ev));
#endif
  }

  void resetFD(int fd) {
    DBG::outl(DBG::Polling, "Poller ", FmtHex(this), " deregister ", fd, " on ", pollFD);
#if __FreeBSD__
    struct kevent ev[2];
    EV_SET(&ev[0], fd, EVFILT_READ, EV_DELETE, 0, 0, 0);
    EV_SET(&ev[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, 0);
    kevent(pollFD, ev, 2, nullptr, 0, nullptr);    // best-effort only
#else // __linux__ below
    epoll_ctl(pollFD, EPOLL_CTL_DEL, fd, nullptr); // best-effort only
#endif
  }
};

class PollerThread : public BasePoller {
  pthread_t pollThread;

protected:
  PollerThread(EventScope& es, const char* n) : BasePoller(es, n) {}
  void start(void *(*loopSetup)(void*)) {
    SYSCALL(pthread_create(&pollThread, nullptr, loopSetup, this));
  }

  template<typename T>
  static inline void pollLoop(T& This);

public:
  ~PollerThread() {
    pollTerminate = true;
    wakeUp(); // use self-pipe trick to terminate poll loop
    SYSCALL(pthread_join(pollThread, nullptr));
  }
  pthread_t getSysID() { return pollThread; }
};

class MasterPoller : public PollerThread {
  int timerFD;
  static void* pollLoopSetup(void*);

public:
#if __FreeBSD__
  static const int extraTimerFD = 1;
#else
  static const int extraTimerFD = 0;
#endif

  MasterPoller(EventScope& es, unsigned long fd, _friend<EventScope>) : PollerThread(es, "MasterPoller") {
    PollerThread::start(pollLoopSetup);
#if __FreeBSD__
    timerFD = fd;
#else
    timerFD = SYSCALLIO(timerfd_create(CLOCK_REALTIME, TFD_NONBLOCK | TFD_CLOEXEC));
    setupFD(timerFD, Input);
#endif
  }

#if __linux__
  ~MasterPoller() { SYSCALL(close(timerFD)); }
#endif

  inline void prePoll(_friend<PollerThread>);

  void setTimer(const Time& reltimeout) {
#if __FreeBSD__
    struct kevent ev;
    EV_SET(&ev, timerFD, EVFILT_TIMER, EV_ADD | EV_ONESHOT, NOTE_USECONDS, reltimeout.toUS(), 0);
    SYSCALL(kevent(pollFD, &ev, 1, nullptr, 0, nullptr));
#else
    itimerspec tval = { {0,0}, reltimeout };
    SYSCALL(timerfd_settime(timerFD, 0, &tval, nullptr));
#endif
  }

  void setupPollFD(int fd, bool change = false) { // (re)set up hierarchical pollling use ONESHOT
#if __FreeBSD__
    struct kevent ev;
    EV_SET(&ev, fd, EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0, 0);
    SYSCALL(kevent(pollFD, &ev, 1, nullptr, 0, nullptr));
#else // __linux__ below
    epoll_event ev;
    ev.events = EPOLLIN | EPOLLONESHOT;
    ev.data.fd = fd;
    SYSCALL(epoll_ctl(pollFD, change ? EPOLL_CTL_MOD : EPOLL_CTL_ADD, fd, &ev));
#endif
  }
};

class PollerFibre : public BasePoller {
  Fibre* pollFibre;
  inline void pollLoop();
  static void pollLoopSetup(PollerFibre*);
public:
  PollerFibre(EventScope&, BaseProcessor&, bool bg = true);
  ~PollerFibre();
  void start();
};

#if TESTING_CLUSTER_POLLER_FIBRE

class ClusterPoller : public PollerFibre {
public:
  ClusterPoller(EventScope& es, BaseProcessor& proc) : PollerFibre(es, proc) {}
};

#else

class ClusterPoller : public PollerThread {
  static void* pollLoopSetup(void*);
public:
  ClusterPoller(EventScope& es, BaseProcessor&) : PollerThread(es, "PollerThread") {}
  void prePoll(_friend<PollerThread>) {}
  void start() { PollerThread::start(pollLoopSetup); }
};

#endif

#endif /* _Poller_h_ */
