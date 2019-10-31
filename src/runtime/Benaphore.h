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
#ifndef _Benaphore_h_
#define _Benaphore_h_ 1

#include <sys/types.h> // ssize_t

template<typename SemType>
class Benaphore {
  volatile ssize_t counter;
  SemType sem;
public:
  Benaphore(ssize_t c = 0) : counter(c), sem(0) {}
  bool empty() { return counter >= 0; }
  bool open() { return counter > 0; }
  ssize_t getValue() { return counter; }

  void reset(ssize_t c = 0) {
    counter = c;
    sem.reset(0);
  }

  bool P() {
    if (__atomic_sub_fetch(&counter, 1, __ATOMIC_SEQ_CST) < 0) sem.P();
    return true;
  }

  bool tryP() {
    ssize_t c = counter;
    return (c >= 1) && __atomic_compare_exchange_n(&counter, &c, c-1, false, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED);
  }

  void V() {
    if (__atomic_add_fetch(&counter, 1, __ATOMIC_SEQ_CST) < 1) sem.V();
  }
};

#endif /* _Benaphore_h_ */
