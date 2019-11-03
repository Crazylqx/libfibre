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
#ifndef _SpinLocks_h_
#define _SpinLocks_h_ 1

#include "runtime/Basics.h"

template<typename T>
static inline bool _CAS(T *ptr, T expected, T desired, int success_memorder = __ATOMIC_SEQ_CST, int failure_memorder = __ATOMIC_RELAXED) {
  T* exp = &expected;
  return __atomic_compare_exchange_n(ptr, exp, desired, false, success_memorder, failure_memorder);
}

// pro: simple
// con: unfair, cache contention
template<size_t SpinStart = 4, size_t SpinEnd = 1024>
class BinaryLock {
protected:
  volatile bool locked;
public:
  BinaryLock() : locked(false) {}
  bool test() const { return locked; }
  bool tryAcquire() {
    if (locked) return false;
    return !__atomic_test_and_set(&locked, __ATOMIC_SEQ_CST);
  }
  void acquire() {
    size_t spin = SpinStart;
    for (;;) {
      if fastpath(!__atomic_test_and_set(&locked, __ATOMIC_SEQ_CST)) break;
      for (size_t i = 0; i < spin; i += 1) Pause();
      if (spin < SpinEnd) spin += spin;
      while (locked) Pause();
    }
  }
  void release() {
    RASSERT0(locked);
    __atomic_clear(&locked, __ATOMIC_SEQ_CST);
  }
} __caligned;

// pro: simple, owner locking
// con: unfair, cache contention
template<size_t SpinStart = 4, size_t SpinEnd = 1024, typename T = uintptr_t, T noOwner=limit<uintptr_t>()>
class BinaryOwnerLock {
  volatile T owner;
  size_t counter;
public:
  BinaryOwnerLock() : owner(noOwner), counter(0) {}
  bool test() const { return owner != noOwner; }
  size_t tryAcquire(T caller) {
    if (owner != caller) {
      if (owner != noOwner) return 0;
      if (!_CAS((T*)&owner, noOwner, caller)) return 0;
    }
    counter += 1;
    return counter;
  }
  size_t acquire(T caller) {
    if (owner != caller) {
      size_t spin = SpinStart;
      for (;;) {
        if fastpath(_CAS((T*)&owner, noOwner, caller)) break;
        for (size_t i = 0; i < spin; i += 1) Pause();
        if (spin < SpinEnd) spin += spin;
        while (owner != noOwner) Pause();
      }
    }
    counter += 1;
    return counter;
  }
  template<bool full = false>
  size_t release(T caller) {
    RASSERT0(owner == caller);
    counter = full ? 0 : (counter - 1);
    if (counter == 0) __atomic_store_n(&owner, noOwner, __ATOMIC_SEQ_CST);
    return counter;
  }
} __caligned;

// pro: fair
// con: cache contention, cache line bouncing
class TicketLock {
  volatile size_t serving;
  size_t ticket;
public:
  TicketLock() : serving(0), ticket(0) {}
  bool test() const { return serving != ticket; }
  bool tryAcquire() {
    if (serving != ticket) return false;
    size_t tryticket = serving;
    return _CAS(&ticket, tryticket, tryticket + 1);
  }
  void acquire() {
    size_t myticket = __atomic_fetch_add(&ticket, 1, __ATOMIC_SEQ_CST);
    while slowpath(myticket != serving) Pause();
  }
  void release() {
    RASSERT0(sword(ticket-serving) > 0);
    __atomic_fetch_add(&serving, 1, __ATOMIC_SEQ_CST);
  }
} __caligned;

// pro: no cache contention -> scalability
// con: storage node, lock bouncing -> use cohorting?
// tested acquire/release memory ordering -> failure?
class MCSLock {
public:
  struct Node {
    Node* volatile next;
    volatile bool wait;
  };
private:
  Node* tail;
public:
  MCSLock() : tail(nullptr) {}
  bool test() const { return tail != nullptr; }
  void acquire(Node& n) {
    n.next = nullptr;
    Node* prev = __atomic_exchange_n(&tail, &n, __ATOMIC_SEQ_CST);
    if (!prev) return;
    n.wait = true;   // store order guaranteed with TSO
    prev->next = &n; // store order guaranteed with TSO
    while slowpath(n.wait) Pause();
  }
  void release(Node& n) {
    RASSERT0(tail != nullptr);
    // could check 'n.next' first, but no memory consistency then
    if (_CAS(&tail, &n, (Node*)nullptr, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) return;
    while slowpath(!n.next) Pause();
    n.next->wait = false;
  }
} __caligned;

// simple spinning RW lock: all racing - starvation possible
class SpinLockRW {
  volatile ssize_t state;                    // -1 writer, 0 open, >0 readers
public:
  SpinLockRW() : state(0) {}
  bool tryAcquireRead() {
    ssize_t s = state;
    return (s >= 0) && __atomic_compare_exchange_n(&state, &s, s+1, false, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED);
  }
  void acquireRead() {
    while slowpath(!tryAcquireRead()) Pause();
  }
  bool tryAcquireWrite() {
    ssize_t s = state;
    return (s == 0) && __atomic_compare_exchange_n(&state, &s, s-1, false, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED);
  }
  void acquireWrite() {
    while slowpath(!tryAcquireWrite()) Pause();
  }
  void release() {
    RASSERT0(state != 0);
    if (state < 0) __atomic_add_fetch(&state, 1, __ATOMIC_SEQ_CST);
    else __atomic_sub_fetch(&state, 1, __ATOMIC_SEQ_CST);
  }
} __caligned;

#endif /* _SpinLocks_h_ */