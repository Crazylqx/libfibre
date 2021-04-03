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
#include "runtime/Basics.h"
#include "runtime/Stats.h"

#include <cstring>
#include <map>

#if TESTING_ENABLE_STATISTICS

static EventScopeStats*  totalEventScopeStats  = nullptr;
static TimerStats*       totalTimerStats       = nullptr;
static PollerStats*      totalPollerStats      = nullptr;
static ClusterStats*     totalClusterStats     = nullptr;
static LoadManagerStats* totalLoadManagerStats = nullptr;
static ProcessorStats*   totalProcessorStats   = nullptr;

struct PrintStatsNode {
  cptr_t object;
  size_t sort;
  const char* name;
  bool operator<(const PrintStatsNode& x) const {
    if (object != x.object) return object < x.object;
    if (sort != x.sort) return sort < x.sort;
    return name[0] < x.name[0];
  }
};

static std::multimap<PrintStatsNode,const StatsObject*> statsMap;

void StatsObject::printRecursive(const StatsObject* o, ostream& os, size_t depth) {
  cptr_t next = nullptr;
  if (o) {
    next = o->object;
    for (size_t i = 0; i < depth; i += 1) os << ' ';
    o->print(os);
    os << std::endl;
    delete o;
    depth += 1;
  }
  for (;;) {
    auto iter = statsMap.lower_bound( {next, 0, ""} );
    if (iter == statsMap.end() || iter->first.object != next) break;
    o = iter->second;
    statsMap.erase(iter);
    printRecursive(o, os, depth);
  }
}

void StatsObject::reset() {}

void StatsObject::resetAll(int) {
  for (StatsObject* o = lst->front(); o != lst->edge(); o = lst->next(*o)) o->reset();
}

void StatsObject::print(ostream& os) const {
  os << name << ' ' << FmtHex(object);
}

void StatsObject::printAll(ostream& os, bool totals) {
  while (!lst->empty()) {
    const StatsObject* o = lst->pop();
    statsMap.insert( {{o->parent, o->sort, o->name}, o} );
  }

  totalEventScopeStats  = new EventScopeStats (nullptr, nullptr, "EventScope ");
  totalPollerStats      = new PollerStats     (nullptr, nullptr, "Poller     ");
  totalTimerStats       = new TimerStats      (nullptr, nullptr, "Timer      ");
  totalClusterStats     = new ClusterStats    (nullptr, nullptr, "Cluster    ");
  totalLoadManagerStats = new LoadManagerStats(nullptr, nullptr, "LoadManager");
  totalProcessorStats   = new ProcessorStats  (nullptr, nullptr, "Processor  ");

  printRecursive(nullptr, os, 0);

  if (!totals) return;

  os << "TOTALS:" << std::endl;

  while (!lst->empty()) {
    const StatsObject* o = lst->pop();
    o->print(os);
    os << std::endl;
    delete o;
  }
}

void EventScopeStats::print(ostream& os) const {
  if (totalEventScopeStats && this != totalEventScopeStats) totalEventScopeStats->aggregate(*this);
  StatsObject::print(os);
  os << " srvconn:" << srvconn << " cliconn:" << cliconn << " resets:" << resets << " calls:" << calls << " fails:" << fails;
}

void PollerStats::print(ostream& os) const {
  if (totalPollerStats && this != totalPollerStats) totalPollerStats->aggregate(*this);
  StatsObject::print(os);
  os << " regs:" << regs << " blocks:" << blocks << " empty:" << empty << " events:" << events;
}

void TimerStats::print(ostream& os) const {
  if (totalTimerStats && this != totalTimerStats) totalTimerStats->aggregate(*this);
  StatsObject::print(os);
  os << " events:" << events;
}

void ClusterStats::print(ostream& os) const {
  if (totalClusterStats && this != totalClusterStats) totalClusterStats->aggregate(*this);
  StatsObject::print(os);
  os << " procs:" << procs << " sleeps:" << sleeps;
}

void LoadManagerStats::print(ostream& os) const {
  if (totalLoadManagerStats && this != totalLoadManagerStats) totalLoadManagerStats->aggregate(*this);
  StatsObject::print(os);
  os << " tasks:" << tasks << " ready (log2):" << ready << " blocked:" << blocked;
}

void ProcessorStats::print(ostream& os) const {
  if (totalProcessorStats && this != totalProcessorStats) totalProcessorStats->aggregate(*this);
  StatsObject::print(os);
  os << " E:" << enq << " U:" << bulk << " D:" << deq << " C:" << correction << " H:" << handover << " S:" << stage << " B:" << borrow << " T:" << steal << " I:" << idle << " W:" << wake;
}

#endif /* TESTING_ENABLE_STATISTICS */
