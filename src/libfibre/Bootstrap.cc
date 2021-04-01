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
#include "libfibre/fibre.h"

#include <atomic>
#include <iostream>
#include <cxxabi.h>   // see _lfAbort
#include <execinfo.h> // see _lfAbort

// various global objects and pointers
static WorkerLock     _dummy1;
WorkerLock*           _lfDebugOutputLock = &_dummy1; // RuntimeDebug.h

#if TESTING_ENABLE_DEBUGGING
static WorkerLock     _dummy2;
static GlobalFredList _dummy3;
WorkerLock*           _lfGlobalFredLock = &_dummy2; // Fibre.h
GlobalFredList*       _lfGlobalFredList = &_dummy3; // Fibre.h
#endif

#if TESTING_ENABLE_STATISTICS
IntrusiveQueue<StatsObject>* StatsObject::lst = nullptr ; // Stats.h
#endif

// ******************** BOOTSTRAP *************************

static const char* DebugOptions[] = {
  "basic",
  "blocking",
  "polling",
  "scheduling",
  "threads",
  "warning",
};

static_assert(sizeof(DebugOptions)/sizeof(char*) == DBG::Level::MaxLevel, "debug options mismatch");

// bootstrap definitions
static std::atomic<int> _bootstrapCounter(0);
#if TESTING_ENABLE_STATISTICS
static std::ios ioFormatFlags(NULL);
#endif

_Bootstrapper::_Bootstrapper() {
  if (++_bootstrapCounter == 1) {
#if TESTING_ENABLE_STATISTICS
    StatsObject::lst = new IntrusiveQueue<StatsObject>;
#endif
    // bootstrap system via event scope
    char* env = getenv("FibreDebugString");
    if (env) DBG::init(DebugOptions, env, false);
  }
}

_Bootstrapper::~_Bootstrapper() {
  if (--_bootstrapCounter == 0) {
#if TESTING_ENABLE_STATISTICS
    if (StatsObject::lst) {
      std::cout.copyfmt(ioFormatFlags);
      if (getenv("FibrePrintStats")) StatsObject::printAll(std::cout, getenv("FibrePrintTotals"));
      delete StatsObject::lst;
    }
#endif
  }
}

static struct _Bootstrapper2 {
  _Bootstrapper2() {
#if TESTING_ENABLE_STATISTICS
    // _Bootstrapper::_Bootstrapper() would be too early - before libstdc++ initializations
    ioFormatFlags.copyfmt(std::cout);
#endif
  }
} _lfBootstrap2;

// definition here ensures that boot strapper object has been created
EventScope* FibreInit(size_t pollerCount, size_t workerCount) {
  return EventScope::bootstrap(pollerCount, workerCount);
}

pid_t FibreFork() {
  Context::CurrEventScope().preFork();
  pid_t ret = fork();
  if (ret == 0) Context::CurrEventScope().postFork(); // child: clean up runtime system
  return ret;
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
      Context::CurrEventScope().setTimer(t);
    }
    TimerQueue& CurrTimerQueue() {
      return Context::CurrEventScope().getTimerQueue();
    }
  }
}
