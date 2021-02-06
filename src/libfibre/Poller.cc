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
#include "libfibre/Poller.h"
#include "libfibre/EventScope.h"

template<bool Blocking>
inline int BasePoller::doPoll() {
#if __FreeBSD__
  static const timespec ts = Time::zero();
  int evcnt = kevent(pollFD, nullptr, 0, events, maxPoll, Blocking ? nullptr : &ts);
#else // __linux__ below
  int evcnt = epoll_wait(pollFD, events, maxPoll, Blocking ? -1 : 0);
#endif
  if (evcnt < 0) { RASSERT(_SysErrno() == EINTR, _SysErrno()); evcnt = 0; } // gracefully handle EINTR
  DBG::outl(DBG::Level::Polling, "Poller ", FmtHex(this), " got ", evcnt, " events from ", pollFD);
  return evcnt;
}

template<bool Enqueue>
inline StackContext* BasePoller::notifyOne(EventType& ev) {
#if __FreeBSD__
  if (ev.filter == EVFILT_READ || ev.filter == EVFILT_TIMER) {
    return eventScope.template unblock<true,Enqueue>(ev.ident, _friend<BasePoller>());
  } else if (ev.filter == EVFILT_WRITE) {
    return eventScope.template unblock<false,Enqueue>(ev.ident, _friend<BasePoller>());
  }
#else // __linux__ below
  if (ev.events & (EPOLLIN | EPOLLPRI | EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
    return eventScope.unblock<true,Enqueue>(ev.data.fd, _friend<BasePoller>());
  }
  if (ev.events & (EPOLLOUT | EPOLLERR)) {
    return eventScope.unblock<false,Enqueue>(ev.data.fd, _friend<BasePoller>());
  }
#endif
  return nullptr;
}

inline void BasePoller::notifyAll(int evcnt) {
  stats->events.add(evcnt);
  for (int e = 0; e < evcnt; e += 1) notifyOne(events[e]);
}

template<typename T>
inline void BaseThreadPoller::pollLoop(T& This) {
  Context::installFake(&This.eventScope, _friend<BaseThreadPoller>());
  while (!This.pollTerminate) {
    This.prePoll(_friend<BaseThreadPoller>());
    This.stats->blocks.count();
    int evcnt = This.template doPoll<true>();
    if (evcnt > 0) This.notifyAll(evcnt);
  }
}

void* MasterPoller::pollLoopSetup(void* This) {
  pollLoop(*reinterpret_cast<MasterPoller*>(This));
  return nullptr;
}

inline void MasterPoller::prePoll(_friend<BaseThreadPoller>) {
  if (eventScope.tryblockTimerFD(timerFD)) {
#if __linux__
    uint64_t count; // read timerFD
    SYSCALL_EQ(read(timerFD, (void*)&count, sizeof(count)), sizeof(count));
#endif
    Time currTime;
    SYSCALL(clock_gettime(CLOCK_REALTIME, &currTime));
    eventScope.checkTimers(currTime);
  }
}

inline void PollerFibre::pollLoop() {
#if TESTING_POLLER_FIBRE_SPIN
  static const size_t SpinMax = TESTING_POLLER_FIBRE_SPIN;
#else
  static const size_t SpinMax = 1;
#endif
  size_t spin = 1;
  while (!pollTerminate) {
    int evcnt = doPoll<false>();
    if fastpath(evcnt > 0) {
      notifyAll(evcnt);
      Fibre::yieldGlobal();
      spin = 1;
    } else if (spin >= SpinMax) {
      stats->blocks.count();
      eventScope.blockPollFD(pollFD);
      spin = 1;
    } else {
      stats->empty.count();
      Fibre::yieldGlobal();
      spin += 1;
    }
  }
}

void PollerFibre::pollLoopSetup(PollerFibre* This) {
  This->eventScope.registerPollFD(This->pollFD);
  This->pollLoop();
}

PollerFibre::PollerFibre(EventScope& es, BaseProcessor& proc, bool bg)
: BasePoller(es, "PollerFibre") {
  pollFibre = new Fibre(proc);
  if (bg) pollFibre->setPriority(LowPriority);
}

PollerFibre::~PollerFibre() {
  pollTerminate = true; // set termination flag, then unblock -> terminate
  eventScope.unblockPollFD(pollFD, _friend<PollerFibre>());
  delete pollFibre;
}

void PollerFibre::start() {
  pollFibre->run(pollLoopSetup, this);
}

void* PollerThread::pollLoopSetup(void* This) {
  pollLoop(*reinterpret_cast<PollerThread*>(This));
  return nullptr;
}
