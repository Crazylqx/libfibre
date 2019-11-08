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
#include "runtime/SpinLocks.h"
#include "libfibre/EventScope.h"

#include <execinfo.h> // see _lfAbort
#include <cxxabi.h>   // see _lfAbort

// default EventScope object
static EventScope* _lfEventScope = nullptr; // EventScope.h

// other global pointers
InternalLock*    _lfDebugOutputLock = nullptr; // RuntimeDebug.h

#if TESTING_ENABLE_DEBUGGING
InternalLock*    _lfGlobalStackLock = nullptr; // StackContext.h
GlobalStackList* _lfGlobalStackList = nullptr; // StackContext.h
#endif

#if TESTING_ENABLE_STATISTICS
IntrusiveQueue<StatsObject>* StatsObject::lst = nullptr; // Stats.h
#endif

// ************** Context / lfbasics.h ********************

thread_local StackContext* volatile Context::currStack   = nullptr;
thread_local OsProcessor*  volatile Context::currProc    = nullptr;
thread_local Cluster*      volatile Context::currCluster = nullptr;
thread_local EventScope*   volatile Context::currScope   = nullptr;

void Context::setCurrStack(StackContext& s, _friend<StackContext>) { currStack = &s; }
StackContext* Context::CurrStack()      { return currStack; }
OsProcessor*  Context::CurrProcessor()  { return currProc; }
Cluster*      Context::CurrCluster()    { return currCluster; }
EventScope*   Context::CurrEventScope() { return currScope; }

// ******************** BOOTSTRAP *************************

// bootstrap counter definition
std::atomic<int> _Bootstrapper::counter(0);

static const char* DebugOptions[] = {
  "basic",
  "blocking",
  "polling",
  "scheduling",
  "threads",
  "warning",
};

static_assert(sizeof(DebugOptions)/sizeof(char*) == DBG::Level::MaxLevel, "debug options mismatch");

//static char debugstring[1024] = "poll,sched";

_Bootstrapper::_Bootstrapper() {
  if (++counter == 1) {
#if TESTING_ENABLE_STATISTICS
    StatsObject::lst = new IntrusiveQueue<StatsObject>;
#endif
    // create locks for debug/assert output
    _lfDebugOutputLock = new InternalLock;
#if TESTING_ENABLE_DEBUGGING
    // create global fibre list
    _lfGlobalStackLock = new InternalLock;
    _lfGlobalStackList = new GlobalStackList;
#endif
    // bootstrap system via event scope
    char* e = getenv("FibreDefaultPollers");
    size_t p = e ? atoi(e) : 1;
    RASSERT0(p > 0);
    _lfEventScope = new EventScope(_friend<_Bootstrapper>(), p);
//    DBG::init(DebugOptions, debugstring, false);
  }
}

_Bootstrapper::~_Bootstrapper() {
  if (--counter == 0) {
    // delete _lfEventScope
    // delete _lfDebugOutputLock;
#if TESTING_ENABLE_DEBUGGING
    delete _lfGlobalStackList;
    delete _lfGlobalStackLock;
#endif
#if TESTING_ENABLE_STATISTICS
    StatsObject::printAll(std::cout);
    delete StatsObject::lst;
#endif
  }
}

// ******************** GLOBAL HELPERS ********************

int _SysErrno() {
  return errno;
}

int& _SysErrnoSet() {
  return errno;
}

void _lfAbort() __noreturn;
void _lfAbort() {
  void* frames[50];
  size_t sz = backtrace(frames, 50);
  char** messages = backtrace_symbols(frames, sz);
  for (size_t i = 0; i < sz; i += 1) {
    char* name = nullptr;
    char* offset = nullptr;
    char* addr = nullptr;
    for (char* c = messages[i]; *c; c += 1) {
      switch (*c) {
        case '(': *c = 0; name = c + 1; break;
        case '+': *c = 0; offset = c + 1; break;
        case ')': *c = 0; addr = c + 1; break;
      }
    }
    std::cout << messages[i] << ':';
    if (name) {
      int status;
      char* demangled = __cxxabiv1::__cxa_demangle(name, 0, 0, &status);
      if (demangled) {
        std::cout << ' ' << demangled;
        free(demangled);
      } else {
        std::cout << ' ' << name;
      }
    }
    if (offset) {
      std::cout << '+' << offset;
    }
    if (addr) {
      std::cout << addr;
    }
    std::cout << std::endl;
  }
  abort();
}

// ******************** ASSERT OUTPUT *********************

#if TESTING_ENABLE_ASSERTIONS
void _SYSCALLabortLock()   { _lfDebugOutputLock->acquire(); }
void _SYSCALLabortUnlock() { _lfDebugOutputLock->release(); }
void _SYSCALLabort()       { _lfAbort(); }
#endif

namespace Runtime {
  namespace Assert {
    void lock() {
      _lfDebugOutputLock->acquire();
    }
    void unlock() {
      _lfDebugOutputLock->release();
    }
    void abort() {
      _lfAbort();
    }
    void print1(sword x) {
      std::cerr << x;
    }
    void print1(const char* x) {
      std::cerr << x;
    }
    void print1(const FmtHex& x) {
      std::cerr << x;
    }
    void printl() {
      std::cerr << std::endl;
    }
  }
  namespace Timer {
    Time now() {
      Time ct;
      SYSCALL(clock_gettime(CLOCK_REALTIME, &ct));
      return ct;
    }
    void newTimeout(const Time& t) {
      CurrEventScope().setTimer(t);
    }
    TimerQueue& CurrTimerQueue() {
      return CurrEventScope().getTimerQueue();
    }
  }
}
