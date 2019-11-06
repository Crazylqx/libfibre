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
#ifndef _lfbasics_h_
#define _lfbasics_h_ 1

// **** bootstrap object needs to come first

#include <atomic>
static class _Bootstrapper {
  static std::atomic<int> counter;
public:
  _Bootstrapper();
  ~_Bootstrapper();
} _lfBootstrap;

#include "runtime/Basics.h"

// **** system processor (here pthread) context

class Cluster;
class EventScope;
class OsProcessor;
class Scheduler;
class StackContext;

// 'noinline' is needed for TLS and then volatile is free anyway...
// http://stackoverflow.com/questions/25673787/making-thread-local-variables-fully-volatile
// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=66631
class Context {
protected: // definitions and initialization are in OsProcessor.cc
  static thread_local StackContext* volatile currStack;
  static thread_local OsProcessor*  volatile currProc;
  static thread_local Cluster*      volatile currCluster;
  static thread_local EventScope*   volatile currScope;
public:
  static void setCurrStack(StackContext& s, _friend<StackContext>) __no_inline;
  static StackContext* CurrStack()      __no_inline;
  static OsProcessor*  CurrProcessor()  __no_inline;
  static Cluster*      CurrCluster()    __no_inline;
  static EventScope*   CurrEventScope() __no_inline;
};

static inline StackContext* CurrStack() {
  StackContext* s = Context::CurrStack();
  RASSERT0(s);
  return s;
}

static inline OsProcessor& CurrProcessor() {
  OsProcessor* p = Context::CurrProcessor();
  RASSERT0(p);
  return *p;
}

static inline Cluster& CurrCluster() {
  Cluster* c = Context::CurrCluster();
  RASSERT0(c);
  return *c;
}

static inline EventScope& CurrEventScope() {
  EventScope* e = Context::CurrEventScope();
  RASSERT0(e);
  return *e;
}

// **** global constants

#ifdef SPLIT_STACK
static const size_t defaultStackSize =  2 * pagesize<1>();
#else
static const size_t defaultStackSize = 16 * pagesize<1>();
#endif
static const size_t stackProtection = pagesize<1>();

#endif /* _lfbasics_h_ */
