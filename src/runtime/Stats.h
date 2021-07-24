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
#ifndef _Stats_h_
#define _Stats_h_ 1

#include "runtime/Container.h"


#include <ostream>
using std::ostream;

typedef long long Number;

#ifdef KERNEL
static inline Number sqrt(Number x) { return x; }
#else
#include <cmath>
#endif

#if TESTING_ENABLE_STATISTICS

class StatsObject : public SingleLink<StatsObject> {
  cptr_t object;
  cptr_t parent;
  const char* name;
  const size_t sort;
  static void printRecursive(const StatsObject* o, ostream& os, size_t depth);
public:
  static IntrusiveQueue<StatsObject>* lst;
  StatsObject(cptr_t o, cptr_t p, const char* n, const size_t s) : object(o), parent(p), name(n), sort(s) { lst->push(*this); }
  virtual ~StatsObject() {}
  virtual void reset();
  static void resetAll(int);
  virtual void print(ostream& os) const;
  static void printAll(ostream& os, bool totals);
};

class Counter {
protected:
  volatile Number cnt;
public:
  Counter() : cnt(0) {}
  operator Number() const { return cnt; }
  void count(Number n = 1) {
    __atomic_add_fetch( &cnt, n, __ATOMIC_RELAXED);
  }
  void aggregate(const Counter& x) {
    cnt += x.cnt;
  }
  void reset() {
    cnt = 0;
  }
}; 

inline ostream& operator<<(ostream& os, const Counter& x) {
  os << ' ' << std::fixed << (Number)x;
  return os;
}

class Average : public Counter {
  using Counter::cnt;
  volatile Number sum;
  volatile Number sqsum;
public:
  Number average() const {
    if (!cnt) return 0;
    return sum/cnt;
  }
  Number variance() const {
    if (!cnt) return 0;
    return sqrt((sqsum - (sum*sum) / cnt) / cnt);
  }
public:
  Average() : sum(0), sqsum(0) {}
  Number operator()() const { return average(); }
  void count(Number val) {
    Counter::count();
    __atomic_add_fetch( &sum, val, __ATOMIC_RELAXED);
    __atomic_add_fetch( &sqsum, val*val, __ATOMIC_RELAXED);
  }
  void aggregate(const Average& x) {
    Counter::aggregate(x);
    sum += x.sum;
    sqsum += x.sqsum;
  }
  void reset() {
    cnt = 0;
    sum = 0;
    sqsum = 0;
  }
};

inline ostream& operator<<(ostream& os, const Average& x) {
  os << (const Counter&)x;
  os << ' ' << std::fixed << x.average() << '/' << x.variance();
  return os;
}

template<size_t N>
struct HashTable {
  Counter bucket[N];
public:
  Number operator[](size_t n) const { return bucket[n]; }
  void count(size_t n) {
    bucket[n % N].count();
  }
  void aggregate(const HashTable<N>& x) {
    for (size_t n = 0; n < N; n += 1) bucket[n].aggregate(x.bucket[n]);
  }
  void reset() {
    for (size_t n = 0; n < N; n += 1) bucket[n].reset();
  }
};

template<size_t N>
inline ostream& operator<<(ostream& os, const HashTable<N>& x) {
  for (size_t n = 0; n < N; n += 1) {
    if (x[n]) os << ' ' << n << ":" << x[n];
  }
  return os;
}

struct Distribution {
  Average average;
  HashTable<bitsize<Number>()> hashTable;
public:
  void count(size_t n) {
    average.count(n);
    hashTable.count(floorlog2(n));
  }
  void aggregate(const Distribution& x) {
    average.aggregate(x.average);
    hashTable.aggregate(x.hashTable);
  }
  void reset() {
    average.reset();
    hashTable.reset();
  }
};

inline ostream& operator<<(ostream& os, const Distribution& x) {
  os << x.average << x.hashTable;
  return os;
}

#else

struct StatsObject {
  StatsObject(cptr_t, cptr_t, const char*, const size_t) {}
};

struct Counter {
  void count(Number = 1) {}
  void aggregate(const Counter&) {}
  void reset() {}
};

struct Average {
  void count(Number) {}
  void aggregate(const Average&) {}
  void reset() {}
};

template<size_t N>
struct HashTable {
  void count(Number) {}
  void aggregate(const HashTable<N>&) {}
  void reset() {}
};

struct Distribution {
  void count(Number) {}
  void aggregate(const Distribution&) {}
  void reset() {}
};

#endif /* TESTING_ENABLE_STATISTICS */

struct EventScopeStats : public StatsObject {
  Counter srvconn;
  Counter cliconn;
  Counter resets;
  Counter calls;
  Counter fails;
  EventScopeStats(cptr_t o, cptr_t p, const char* n = "EventScope   ") : StatsObject(o, p, n, 0) {}
  void print(ostream& os) const;
  void aggregate(const EventScopeStats& x) {
    srvconn.aggregate(x.srvconn);
    cliconn.aggregate(x.cliconn);
    resets.aggregate(x.resets);
    calls.aggregate(x.calls);
    fails.aggregate(x.fails);
  }
  virtual void reset() {
    srvconn.reset();
    cliconn.reset();
    resets.reset();
    calls.reset();
    fails.reset();
  }
};

struct PollerStats : public StatsObject {
  Counter regs;
  Counter blocks;
  Counter empty;
  Distribution events;
  PollerStats(cptr_t o, cptr_t p, const char* n = "Poller") : StatsObject(o, p, n, 0) {}
  void print(ostream& os) const;
  void aggregate(const PollerStats& x) {
    regs.aggregate(x.regs);
    blocks.aggregate(x.blocks);
    empty.aggregate(x.empty);
    events.aggregate(x.events);
  }
  virtual void reset() {
    regs.reset();
    blocks.reset();
    empty.reset();
    events.reset();
  }
};

struct TimerStats : public StatsObject {
  Distribution events;
  TimerStats(cptr_t o, cptr_t p, const char* n = "Timer       ") : StatsObject(o, p, n, 1) {}
  void print(ostream& os) const;
  void aggregate(const TimerStats& x) {
    events.aggregate(x.events);
  }
  virtual void reset() {
    events.reset();
  }
};

struct ClusterStats : public StatsObject {
  Counter procs;
  Counter sleeps;
  ClusterStats(cptr_t o, cptr_t p, const char* n = "Cluster     ") : StatsObject(o, p, n, 2) {}
  void print(ostream& os) const;
  void aggregate(const ClusterStats& x) {
    procs.aggregate(x.procs);
    sleeps.aggregate(x.sleeps);
  }
  virtual void reset() {
    procs.reset();
    sleeps.reset();
  }
};

struct IdleManagerStats : public StatsObject {
  Distribution ready;
  Distribution blocked;
  IdleManagerStats(cptr_t o, cptr_t p, const char* n = "IdleManager") : StatsObject(o, p, n, 1) {}
  void print(ostream& os) const;
  void aggregate(const IdleManagerStats& x) {
    ready.aggregate(x.ready);
    blocked.aggregate(x.blocked);
  }
  virtual void reset() {
    ready.reset();
    blocked.reset();
  }
};

struct ProcessorStats : public StatsObject {
  Counter enq;
  Counter deq;
  Counter handover;
  Counter stage;
  Counter borrow;
  Counter steal;
  Counter idle;
  Counter wake;
  ProcessorStats(cptr_t o, cptr_t p, const char* n = "Processor  ") : StatsObject(o, p, n, 2) {}
  void print(ostream& os) const;
  void aggregate(const ProcessorStats& x) {
    enq.aggregate(x.enq);
    deq.aggregate(x.deq);
    handover.aggregate(x.handover);
    stage.aggregate(x.stage);
    borrow.aggregate(x.borrow);
    steal.aggregate(x.steal);
    idle.aggregate(x.idle);
    wake.aggregate(x.wake);
  }
  virtual void reset() {
    enq.reset();
    deq.reset();
    handover.reset();
    stage.reset();
    borrow.reset();
    steal.reset();
    idle.reset();
    wake.reset();
  }
};

/*
  sort order for output:
  0 EventScope
  0 Poller
  1 Timer
  2 Cluster
  0  Poller
  1  IdleManager
  2  Processor
  0   Poller
*/

#endif /* _Stats_h_ */
