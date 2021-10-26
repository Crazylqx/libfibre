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
#include <csignal>
#include <iostream>
#include <cxxabi.h>   // see _lfAbort
#include <execinfo.h> // see _lfAbort

// various global objects and pointers
static WorkerLock     _dummy1;
WorkerLock*           _lfDebugOutputLock = &_dummy1; // RuntimeDebug.h
size_t                _lfPagesize = 0;

#if TESTING_ENABLE_DEBUGGING
static WorkerLock              _dummy2;
static FredList<FredDebugLink> _dummy3;
WorkerLock*                    _lfFredDebugLock = &_dummy2; // Fibre.h
FredList<FredDebugLink>*       _lfFredDebugList = &_dummy3; // Fibre.h
size_t                         _lfFredDebugLink = FredDebugLink;
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

static std::ios ioFormatFlags(nullptr);

static void _lfPrintStats() {
  std::cout.copyfmt(ioFormatFlags);
  char* env = getenv("FibrePrintStats");
  if (env) FredStats::StatsPrint(std::cout, env[0] == 't' || env[0] == 'T');
}

EventScope* FibreInit(size_t pollerCount, size_t workerCount) {
  _lfPagesize = sysconf(_SC_PAGESIZE);
  ioFormatFlags.copyfmt(std::cout);
  SYSCALL(atexit(_lfPrintStats));
  char* env = getenv("FibreDebugString");
  if (env) DBG::init(DebugOptions, env, false);
  env = getenv("FibreStatsSignal");
  if (env) {
    int signum = strtol(env, NULL, 10);
    if (signum == 0) signum = SIGUSR1;
    struct sigaction sa;
    sa.sa_handler = FredStats::StatsClear;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    SYSCALL(sigaction(signum, &sa, 0));
  }
  env = getenv("FibrePollerCount");
  if (env) {
    int cnt = atoi(env);
    if (cnt > 0) pollerCount = cnt;
  }
  env = getenv("FibreWorkerCount");
  if (env) {
    int cnt = atoi(env);
    if (cnt > 0) workerCount = cnt;
  }
  return EventScope::bootstrap(pollerCount, workerCount);
}

pid_t FibreFork() {
  Context::CurrEventScope().preFork();
  pid_t ret = fork();
  if (ret == 0) {
    FredStats::StatsReset();
    Context::CurrEventScope().postFork(); // child: clean up runtime system
  }
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
