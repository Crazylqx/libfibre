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
#ifndef _Basics_h_
#define _Basics_h_ 1

#include "runtime/Assertions.h"
#include "runtime/FloatingPoint.h"
#include "runtime/Platform.h"

class NoObject {
  NoObject() = delete;                           // no creation
  NoObject(const NoObject&) = delete;            // no copy
  NoObject& operator=(const NoObject&) = delete; // no assignment
};

template<typename Friend> class _friend {
  friend Friend;
  _friend() {}
};

typedef void (*funcvoid0_t)();
typedef void (*funcvoid1_t)(ptr_t);
typedef void (*funcvoid2_t)(ptr_t, ptr_t);
typedef void (*funcvoid3_t)(ptr_t, ptr_t, ptr_t);

typedef void* (*funcptr0_t)();
typedef void* (*funcptr1_t)(ptr_t);
typedef void* (*funcptr2_t)(ptr_t, ptr_t);
typedef void* (*funcptr3_t)(ptr_t, ptr_t, ptr_t);

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
static inline constexpr T limit() {
  return ~T(0);
}

template <typename T>
static inline constexpr T slimit() {
  return ~T(0) >> 1;
}

template <typename T>
static inline constexpr size_t bitsize() {
  return sizeof(T) * charbits;
}

template <typename T>
static inline constexpr T bitmask(unsigned int Width) {
  return Width == bitsize<T>() ? limit<T>() : pow2<T>(Width) - 1;
}

template <typename T>
static inline constexpr T bitmask(unsigned int Pos, unsigned int Width) {
  return bitmask<T>(Width) << Pos;
}

template <typename T, unsigned int Pos, unsigned int Width>
struct BitString {
  static_assert( Pos + Width <= 8*sizeof(T), "illegal parameters" );
  constexpr T operator()() const { return bitmask<T>(Pos,Width); }
  constexpr T put(T f) const { return (f & bitmask<T>(Width)) << Pos; }
  constexpr T get(T f) const { return (f >> Pos) & bitmask<T>(Width); }
  constexpr T excl(T f) const { return f & ~bitmask<T>(Pos,Width); }
};

template<bool atomic=false>
static inline void bit_set(mword& a, size_t idx) {
  mword b = mword(1) << idx;
  if (atomic) __atomic_or_fetch(&a, b, __ATOMIC_RELAXED);
  else a |= b;
}

template<bool atomic=false>
static inline void bit_clr(mword& a, size_t idx) {
  mword b = ~(mword(1) << idx);
  if (atomic) __atomic_and_fetch(&a, b, __ATOMIC_RELAXED);
  else a &= b;
}

template<bool atomic=false>
static inline void bit_flp(mword& a, size_t idx) {
  mword b = mword(1) << idx;
  if (atomic) __atomic_xor_fetch(&a, b, __ATOMIC_RELAXED);
  else a ^= b;
}

static inline bool bit_tst(const mword& a, size_t idx) {
  mword b = mword(1) << idx;
  return a & b;
}

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

class Time : public timespec {
  friend std::ostream& operator<<(std::ostream&, const Time&);
public:
  static const long long NSEC = 1000000000ll;
  static const long long USEC = 1000000ll;
  static const long long MSEC = 1000ll;
  Time() {}
  Time(const volatile Time& t) : timespec({t.tv_sec, t.tv_nsec}) {}
  Time(const Time& t) : timespec({t.tv_sec, t.tv_nsec}) {}
  Time(time_t sec, long nsec) : timespec({sec, nsec}) {}
  Time(const timespec& t) : timespec(t) {}
  static const Time zero() { return Time(0,0); }
  static Time fromMS(long long ms) {
    return Time(ms / 1000, (ms % 1000) * 1000000);
  }
  long long toMS() const {
    return 1000ll * tv_sec + tv_nsec / 1000000;
  }
  static Time fromUS(long long us) {
    return Time(us / 1000000, (us % 1000000) * 1000);
  }
  long long toUS() const {
    return 1000000ll * tv_sec + tv_nsec / 1000;
  }
  Time& operator=(const Time& t) {
    tv_sec  = t.tv_sec;
    tv_nsec = t.tv_nsec;
    return *this;
  }
  Time& operator+=(const Time& t) {
    tv_sec  += t.tv_sec;
    tv_nsec += t.tv_nsec;
    if (tv_nsec > NSEC) { tv_sec += 1; tv_nsec -= NSEC; }
    return *this;
  }
  Time operator+(const Time& t) const {
    Time v = {tv_sec + t.tv_sec, tv_nsec + t.tv_nsec};
    if (v.tv_nsec > NSEC) { v.tv_sec += 1; v.tv_nsec -= NSEC; }
    return v;
  }
  Time& operator-=(const Time& t) {
    tv_nsec -= t.tv_nsec;
    tv_sec  -= t.tv_sec;
    if (tv_nsec < 0) { tv_sec -= 1; tv_nsec += NSEC; }
    return *this;
  }
  Time operator-(const Time& t) const {
    Time v = {tv_sec - t.tv_sec, tv_nsec - t.tv_nsec};
    if (v.tv_nsec < 0) { v.tv_sec -= 1; v.tv_nsec += NSEC; }
    return v;
  }
  bool operator==(const Time& t) const {
    return tv_sec == t.tv_sec && tv_nsec == t.tv_nsec;
  }
  bool operator<(const Time& t) const {
    return (tv_sec == t.tv_sec) ? tv_nsec < t.tv_nsec : tv_sec < t.tv_sec;
  }
  bool operator<=(const Time& t) const {
    return (tv_sec == t.tv_sec) ? tv_nsec <= t.tv_nsec : tv_sec <= t.tv_sec;
  }
  bool operator>(const Time& t) const {
    return (tv_sec == t.tv_sec) ? tv_nsec > t.tv_nsec : tv_sec > t.tv_sec;
  }
  bool operator>=(const Time& t) const {
    return (tv_sec == t.tv_sec) ? tv_nsec >= t.tv_nsec : tv_sec >= t.tv_sec;
  }
};

extern inline std::ostream& operator<<(std::ostream& os, const Time& t) {
  os << t.tv_sec << '.' << std::setw(9) << std::setfill('0') << t.tv_nsec;
  return os;
}

#endif /* _Basics_h_ */
