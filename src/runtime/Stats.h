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
#ifndef _Stats_h_
#define _Stats_h_ 1

#include "runtime/IntrusiveContainers.h"

#include <ostream>
using std::ostream;

typedef long long Number;

#if TESTING_ENABLE_STATISTICS

class StatsObject : public SingleLink<StatsObject> {
  void* obj;
  const char* name;
public:
  static IntrusiveQueue<StatsObject>* lst;
  StatsObject(void* o, const char* n = "Object") : obj(o), name(n) { lst->push(*this); }
  virtual ~StatsObject() {}
  virtual bool print(ostream& os);
  static void printAll(ostream& os);
};

class Counter {
protected:
  volatile Number cnt;
public:
  Counter() : cnt(0) {}
  operator Number() const { return cnt; }
  Number operator()() const { return sum(); }
  Number sum() const {
    return cnt;
  }
  void count(Number n = 1) {
    __atomic_add_fetch( &cnt, n, __ATOMIC_RELAXED);
  }
  void aggregate(const Counter& x) {
    cnt += x.cnt;
  }
}; 

inline ostream& operator<<(ostream& os, const Counter& x) {
  os << ' ' << std::fixed << x.sum();
  return os;
}

class Average : public Counter {
  volatile Number sum;
  volatile Number sqsum;
  using Counter::cnt;
public:
  Average() : sum(0), sqsum(0) {}
  Number operator()() const { return average(); }
  Number average() const {
    if (!cnt) return 0;
    return sum/cnt;
  }
  Number variance() const {
    if (!cnt) return 0;
    return (sqsum - (sum*sum) / cnt) / cnt;
  }
  void add(Number val) {
    __atomic_add_fetch( &sum, val, __ATOMIC_RELAXED);
    __atomic_add_fetch( &sqsum, val*val, __ATOMIC_RELAXED);
    Counter::count();
  }
  void aggregate(const Average& x) {
    sum += x.sum;
    sqsum += x.sqsum;
    Counter::aggregate(x);
  }
};

inline ostream& operator<<(ostream& os, const Average& x) {
  os << (const Counter&)x;
  os << ' ' << std::fixed << x.average() << '/' << x.variance();
  return os;
}

template<size_t N>
class HashTable {
  Counter bucket[N];
public:
  Number operator[](size_t n) const { return bucket[n]; }
  void count(size_t n) {
    bucket[n % N].count();
  }
};

template<size_t N>
inline ostream& operator<<(ostream& os, const HashTable<N>& x) {
  for (size_t n = 0; n < N; n += 1) {
    if (x[n]) os << n << ":" << x[n] << ' ';
  }
  return os;
}

#else

class StatsObject {
public:
  StatsObject(void*, const char*) {}
};

class Counter {
public:
  void count(Number n = 1) {}
  void aggregate(const Counter& x) {}
}; 

class Average : protected Counter {
public:
  void add(Number val) {}
  void aggregate(const Average& x) {}
};

template<size_t N>
class HashTable {
public:
  void count(size_t n) {}
};

#endif /* TESTING_ENABLE_STATISTICS */

struct ProcessorStats : public StatsObject {
  Counter enq;
  Average bulk;
  Counter deq;
  Counter correction;
  Counter handover;
  Counter stage;
  Counter borrow;
  Counter steal;
  Counter idle;
  Counter wake;
  ProcessorStats(void* o, const char* n = "Processor") : StatsObject(o, n) {}
  bool print(ostream& os);
  void aggregate(const ProcessorStats& x) {
    enq.aggregate(x.enq);
    deq.aggregate(x.deq);
    correction.aggregate(x.correction);
    handover.aggregate(x.handover);
    stage.aggregate(x.stage);
    borrow.aggregate(x.borrow);
    steal.aggregate(x.steal);
    idle.aggregate(x.idle);
    wake.aggregate(x.wake);
  }
};

struct LoadManagerStats : public StatsObject {
  Counter tasks;
  HashTable<64> blocks;
  LoadManagerStats(void* o, const char* n = "LoadManager") : StatsObject(o, n) {}
  bool print(ostream& os);
  void aggregate(const LoadManagerStats& x) {
    tasks.aggregate(x.tasks);
  }
};

struct ClusterStats : public StatsObject {
  Counter procs;
  Counter pauses;
  Counter sleeps;
  ClusterStats(void* o, const char* n = "Cluster") : StatsObject(o, n) {}
  bool print(ostream& os);
  void aggregate(const ClusterStats& x) {
    procs.aggregate(x.procs);
    pauses.aggregate(x.pauses);
    sleeps.aggregate(x.sleeps);
  }
};

struct TimerStats : public StatsObject {
  Average events;
  TimerStats(void* o, const char* n = "Timer") : StatsObject(o, n) {}
  bool print(ostream& os);
  void aggregate(const TimerStats& x) {
    events.aggregate(x.events);
  }
};

struct ConnectionStats : public StatsObject {
  Counter srvconn;
  Counter cliconn;
  Counter resets;
  ConnectionStats(void* o, const char* n = "Connections") : StatsObject(o, n) {}
  bool print(ostream& os);
  void aggregate(const ConnectionStats& x) {
    srvconn.aggregate(x.srvconn);
    cliconn.aggregate(x.cliconn);
    resets.aggregate(x.resets);
  }
};

struct PollerStats : public StatsObject {
  Counter blocks;
  Counter empty;
  Average events;
  PollerStats(void* o, const char* n = "Poller") : StatsObject(o, n) {}
  bool print(ostream& os);
  void aggregate(const PollerStats& x) {
    blocks.aggregate(x.blocks);
    empty.aggregate(x.empty);
    events.aggregate(x.events);
  }
};

#endif /* _Stats_h_ */
