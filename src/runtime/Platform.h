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
#ifndef _Platform_h_
#define _Platform_h_ 1

#include "runtime/testoptions.h"

#include <cstddef>
#include <cstdint>  

#if defined(__x86_64__)

#if defined(__clang__) // avoid include file problems
static inline void Pause(void) { asm volatile("pause"); }
#else
#include <xmmintrin.h> // _mm_pause
static inline void Pause(void) { _mm_pause(); }
#endif

static inline void MemoryFence(void) { asm volatile("mfence" ::: "memory"); }

typedef uint64_t mword;
typedef  int64_t sword;

typedef uintptr_t vaddr;
typedef uintptr_t paddr;

typedef       void*    ptr_t;
typedef const void*   cptr_t;

typedef       char     buf_t;
typedef       char* bufptr_t;

static const size_t charbits       = 8;

static const size_t pageoffsetbits = 12;
static const size_t pagetablebits  = 9;
static const size_t pagelevels     = 4;
static const size_t pagebits       = pageoffsetbits + pagetablebits * pagelevels;
static const size_t framebits      = pageoffsetbits + 40;
static const size_t ptentries      = 1 << pagetablebits;

static const vaddr stackAlignment  = 16;

// expressions
#define fastpath(x) (__builtin_expect((bool(x)),true))
#define slowpath(x) (__builtin_expect((bool(x)),false))
// data structures
#define __packed     __attribute__((__packed__))
#define __section(x) __attribute__((__section__(x)))
#define __aligned(x) __attribute__((__aligned__(x)))
#if defined(KERNEL)
#define __caligned   __attribute__((__aligned__(128)))
#else
#define __caligned
#endif
// functions
#define __yes_inline __attribute__((__always_inline__))
#define __no_inline  __attribute__((__noinline__))
#define __noreturn   __attribute__((__noreturn__))

static inline void unreachable() __noreturn;
static inline void unreachable() {
  __builtin_unreachable();
  __builtin_trap();
}

class FloatingPointFlags { // FP (x87/SSE) control/status words (ABI Section 3.2.3, Fig 3.4)
  uint32_t csr;
  uint32_t cw;
public:
  FloatingPointFlags(uint32_t csr = 0x1FC0, uint32_t cw = 0x037F) : csr(csr), cw(cw) {}
  FloatingPointFlags(bool s) { if (s) save(); }
  void save() {
    asm volatile("stmxcsr %0" : "=m"(csr) :: "memory");
    asm volatile("fnstcw  %0" : "=m"(cw) :: "memory");
  }
  void restore() {
    asm volatile("ldmxcsr %0" :: "m"(csr) : "memory");
    asm volatile("fldcw   %0" :: "m"(cw) : "memory");
  }
};

class FloatingPointContext {
  char fpu[512] __attribute__((__aligned__(16)));     // alignment required for fxsave/fxrstor
  enum State { Init = 0, Clean = 1, Dirty = 2 } state;
public:
  FloatingPointContext() : state(Init) {}
  void setClean() { state = Clean; }
  bool  isClean() const { return state == Clean; }
  static void initCPU() {
    asm volatile("finit" ::: "memory");
  }
  void save() {    // TODO: later use XSAVEOPTS for complete SSE/AVX/etc state!
    asm volatile("fxsave %0" : "=m"(fpu) :: "memory");
    state = Dirty;
  }
  void restore() { // TODO: later use XRSTOR for complete SSE/AVX/etc state!
    if (state == Dirty) asm volatile("fxrstor %0" :: "m"(fpu) : "memory");
    else if (state == Init) initCPU();
    state = Clean;
  }
};

#else
#error unsupported architecture: only __x86_64__ supported at this time
#endif

#endif /* _Platform_h_ */
