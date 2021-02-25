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
#ifndef _Platform_h_
#define _Platform_h_ 1

#include "runtime/testoptions.h"

#include <stddef.h>
#include <stdint.h>

typedef uintptr_t vaddr;
typedef uintptr_t paddr;

typedef       void*    ptr_t;
typedef const void*   cptr_t;

typedef       char     buf_t;
typedef       char* bufptr_t;

// expressions
#define fastpath(x) (__builtin_expect((bool(x)),true))
#define slowpath(x) (__builtin_expect((bool(x)),false))
// functions
#define __yes_inline __attribute__((__always_inline__))
#define __no_inline  __attribute__((__noinline__))
#define __noreturn   __attribute__((__noreturn__))
// data structures
#define __packed     __attribute__((__packed__))
#define __section(x) __attribute__((__section__(x)))
#define __aligned(x) __attribute__((__aligned__(x)))
#if defined(KERNEL)
#define __caligned   __attribute__((__aligned__(128)))
#else
#define __caligned
#endif

static inline void unreachable() __noreturn;
static inline void unreachable() {
  __builtin_unreachable();
  __builtin_trap();
}

template <typename T>
static inline constexpr T limit() {
  return ~T(0);
}

template <typename T>
static inline constexpr T slimit() {
  return ~T(0) >> 1;
}

template <typename T>
static inline constexpr T pow2( unsigned int x ) {
  return T(1) << x;
}

template <typename T>
static inline constexpr bool ispow2( T x ) {
  return (x & (x - 1)) == 0;
}

template <typename T>
static inline constexpr T align_up( T x, T a ) {
  return (x + a - 1) & (~(a - 1));
}

template <typename T>
static inline constexpr T align_down( T x, T a ) {
//  return x - (x % a);
  return x & (~(a - 1));
}

template <typename T>
static inline constexpr bool aligned( T x, T a ) {
//  return (x % a) == 0;
  return (x & (a - 1)) == 0;
}

template <typename T>
static inline constexpr T divup( T n, T d ) {
  return ((n - 1) / d) + 1;
}

template <typename T>
static inline constexpr size_t bitsize(); // see below for platform-dependent implementation

template <typename T>
static inline constexpr T bitmask(unsigned int Width) {
  return Width == bitsize<T>() ? limit<T>() : pow2<T>(Width) - 1;
}

template <typename T>
static inline constexpr T bitmask(unsigned int Pos, unsigned int Width) {
  return bitmask<T>(Width) << Pos;
}

#if defined(__x86_64__)

#if defined(__clang__) || defined(__cforall) // avoid include file problems
static inline void Pause(void) { asm volatile("pause"); }
#else
#include <xmmintrin.h> // _mm_pause
static inline void Pause(void) { _mm_pause(); }
#endif

static inline void MemoryFence(void) { asm volatile("mfence" ::: "memory"); }

typedef uint64_t mword;
typedef  int64_t sword;

static const vaddr stackAlignment  = 16;

static const size_t pageoffsetbits = 12;
static const size_t pagetablebits  = 9;
static const size_t pagelevels     = 4;
#if defined(__cplusplus)
static const size_t pagebits       = pageoffsetbits + pagetablebits * pagelevels;
static const size_t framebits      = pageoffsetbits + 40;
static const size_t ptentries      = 1 << pagetablebits;
#endif

template <typename T> static inline constexpr size_t bitsize() { return sizeof(T) * 8; }

// depending on the overall code complexity, the loop can be unrolled at -O3
// "=&r"(scan) to mark as 'earlyclobber': modified before all input processed
// "+r"(newmask) to keep newmask = mask
template<size_t N, bool FindNext = false>
static inline mword multiscan(const mword* data, size_t idx = 0, bool findset = true) {

  mword result = 0;
  mword mask = ~mword(0);
  mword newmask = mask;

  for (size_t i = FindNext ? 0 : (idx / bitsize<mword>()); i < N; i += 1) {
    mword scan;
    mword datafield = (findset ? data[i] : ~data[i]);
    if (FindNext && (i == idx / bitsize<mword>())) datafield &= ~bitmask<mword>(idx % bitsize<mword>());
    asm volatile("\
      bsfq %2, %0\n\t\
      cmovzq %3, %0\n\t\
      cmovnzq %4, %1"
    : "=&r"(scan), "+r"(newmask)
    : "rm"(datafield), "r"(bitsize<mword>()), "r"(mword(0))
    : "cc");
    result += scan & mask;
    mask = newmask;
  }

  return result;
}

template<size_t N>
static inline mword multiscan_next(const mword* data, size_t idx = 0, bool findset = true) {
  return multiscan<N,true>(data, idx, findset);
}

// depending on the overall code complexity, the loop can be unrolled at -O3
// "=&r"(scan) to mark as 'earlyclobber': modified before all input processed
// "+r"(newmask) to keep newmask = mask
template<size_t N>
static inline mword multiscan_rev(const mword* data, bool findset = true) {
  mword result = 0;
  mword mask = ~mword(0);
  mword newmask = mask;
  size_t i = N;
  do {
    i -= 1;
    mword scan;
    mword datafield = (findset ? data[i] : ~data[i]);
    asm volatile("\
      bsrq %2, %0\n\t\
      cmovzq %3, %0\n\t\
      cmovnzq %4, %1"
    : "=&r"(scan), "+r"(newmask)
    : "rm"(datafield), "r"(mword(0)), "r"(mword(0))
    : "cc");
    result += (scan & mask) + (bitsize<mword>() & ~mask);
    mask = newmask;
  } while (i != 0);
  return result;
}

#else
#error unsupported architecture: only __x86_64__ supported at this time
#endif

template<unsigned int N>
static constexpr size_t pagesizebits() {
  static_assert( N <= pagelevels + 1, "page level template violation" );
  return pageoffsetbits + (N-1) * pagetablebits;
}

template<unsigned int N>
static constexpr size_t pagesize() {
  static_assert( N <= pagelevels, "page level template violation" );
  return pow2<size_t>(pagesizebits<N>());
}

template<unsigned int N>
static constexpr size_t pageoffset(uintptr_t addr) {
  static_assert( N <= pagelevels, "page level template violation" );
  return addr & bitmask<uintptr_t>(pagesizebits<N>());
}

#endif /* _Platform_h_ */
