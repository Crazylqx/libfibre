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
#include "runtime/Basics.h"
#include "runtime/Stats.h"

#if TESTING_ENABLE_STATISTICS

static ProcessorStats*   totalProcessorStats   = nullptr;
static LoadManagerStats* totalLoadManagerStats = nullptr;
static ClusterStats*     totalClusterStats     = nullptr;
static TimerStats*       totalTimerStats       = nullptr;
static ConnectionStats*  totalConnectionStats  = nullptr;
static PollerStats*      totalPollerStats      = nullptr;

bool StatsObject::print(ostream& os) {
  os << name << ' ' << FmtHex(obj);
  return true;
}

void StatsObject::printAll(ostream& os) {
  totalProcessorStats   = new ProcessorStats  (0, "Processor (total)");
  totalLoadManagerStats = new LoadManagerStats(0, "LoadManager (total)");
  totalClusterStats     = new ClusterStats    (0, "Cluster (total)");
  totalTimerStats       = new TimerStats      (0, "Timer (total)");
  totalConnectionStats  = new ConnectionStats (0, "Connections (total)");
  totalPollerStats      = new PollerStats     (0, "Poller (total)");
  while (!lst->empty()) {
    StatsObject* o = lst->pop();
    if (o->print(os)) os << std::endl;
    delete o;
  }
}

bool ProcessorStats::print(ostream& os) {
  if (totalProcessorStats && this != totalProcessorStats) totalProcessorStats->aggregate(*this);
  if (enq + bulk + deq + correction + handover + stage + steal + borrow == 0) return false;
  StatsObject::print(os);
  os << " E:" << enq << " U:" << bulk << " D:" << deq << " C:" << correction << " H:" << handover << " S:" << stage << " B:" << borrow << " T:" << steal << " I:" << idle << " W:" << wake;
  return true;
}

bool LoadManagerStats::print(ostream& os) {
  if (totalLoadManagerStats && this != totalLoadManagerStats) totalLoadManagerStats->aggregate(*this);
  if (tasks == 0) return false;
  StatsObject::print(os);
  os << tasks << blocks;
  return true;
}

bool ClusterStats::print(ostream& os) {
  if (totalClusterStats && this != totalClusterStats) totalClusterStats->aggregate(*this);
  if (sleeps == 0) return false;
  StatsObject::print(os);
  os << procs << sleeps;
  return true;
}

bool TimerStats::print(ostream& os) {
  if (totalTimerStats && this != totalTimerStats) totalTimerStats->aggregate(*this);
  if (events == 0) return false;
  StatsObject::print(os);
  os << events;
  return true;
}

bool ConnectionStats::print(ostream& os) {
  if (totalConnectionStats && this != totalConnectionStats) totalConnectionStats->aggregate(*this);
  if (srvconn + cliconn + resets == 0) return false;
  StatsObject::print(os);
  os << " server:" << srvconn << " client:" << cliconn << " resets: " << resets;
  return true;
}

bool PollerStats::print(ostream& os) {
  if (totalPollerStats && this != totalPollerStats) totalPollerStats->aggregate(*this);
  if (empty + events == 0) return false;
  StatsObject::print(os);
  os << regs << blocks << empty << events;
  return true;
}

#endif /* TESTING_ENABLE_STATISTICS */
