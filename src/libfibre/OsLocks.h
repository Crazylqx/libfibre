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
#ifndef _OsLocks_h_
#define _OsLocks_h_ 1

#include "runtime/Basics.h"
#include "runtime/ScopedLocks.h"
#include "libfibre/syscall_macro.h"

#include <pthread.h>
#include <semaphore.h>

template<size_t SpinStart, size_t SpinEnd, size_t SpinCount>
class OsLock {
  pthread_mutex_t mutex;
  friend class OsCondition;
public:
  OsLock() : mutex(PTHREAD_MUTEX_INITIALIZER) {}
  ~OsLock() {
    SYSCALL(pthread_mutex_destroy(&mutex));
  }
  bool tryAcquire() {
    return pthread_mutex_trylock(&mutex) == 0;
  }
  void acquire() {
    for (size_t cnt = 0; cnt < SpinCount; cnt += 1) {
      for (size_t spin = SpinStart; spin <= SpinEnd; spin += spin) {
        if fastpath(tryAcquire()) return;
        for (size_t i = 0; i < spin; i += 1) Pause();
      }
    }
    SYSCALL(pthread_mutex_lock(&mutex));
  }
  bool acquire(const Time& timeout) {
    return TRY_SYSCALL(pthread_mutex_timedlock(&mutex, &timeout), ETIMEDOUT) == 0;
  }
  void release() {
    SYSCALL(pthread_mutex_unlock(&mutex));
  }
  bool test() {
    if (!tryAcquire()) return true;
    release();
    return false;
  }
};

class OsCondition {
  pthread_cond_t cond;
public:
  OsCondition() : cond(PTHREAD_COND_INITIALIZER) {}
  ~OsCondition() {
    SYSCALL(pthread_cond_destroy(&cond));
  }
  template<typename Lock>
  void clear(Lock& lock) {
    SYSCALL(pthread_cond_broadcast(&cond));
    lock.release();
  }
  template<typename Lock>
  void wait(Lock& lock) {
    SYSCALL(pthread_cond_wait(&cond, &lock.mutex));
  }
  template<typename Lock>
  bool wait(Lock& lock, const Time& timeout) {
    return TRY_SYSCALL(pthread_cond_timedwait(&cond, &lock.mutex, &timeout), ETIMEDOUT) == 0;
  }
  void signal() {
    SYSCALL(pthread_cond_signal(&cond));
  }
};

template<size_t SpinStartRead, size_t SpinEndRead, size_t SpinCountRead, size_t SpinStartWrite, size_t SpinEndWrite, size_t SpinCountWrite>
class OsLockRW {
  pthread_rwlock_t rwlock;
public:
  OsLockRW() {
    SYSCALL(pthread_rwlock_init(&rwlock, nullptr));
  }
  ~OsLockRW() {
    SYSCALL(pthread_rwlock_destroy(&rwlock));
  }
  bool tryAcquireRead() {
    return pthread_rwlock_tryrdlock(&rwlock) == 0;
  }
  void acquireRead() {
    for (size_t cnt = 0; cnt < SpinCountRead; cnt += 1) {
      for (size_t spin = SpinStartRead; spin <= SpinEndRead; spin += spin) {
        if fastpath(tryAcquireRead()) return;
        for (size_t i = 0; i < spin; i += 1) Pause();
      }
    }
    SYSCALL(pthread_rwlock_rdlock(&rwlock));
  }
  bool acquireRead(const Time& timeout) {
    return TRY_SYSCALL(pthread_rwlock_timedrdlock(&rwlock, &timeout), ETIMEDOUT) == 0;
  }
  bool tryAcquireWrite() {
    return pthread_rwlock_trywrlock(&rwlock) == 0;
  }
  void acquireWrite() {
    for (size_t cnt = 0; cnt < SpinCountWrite; cnt += 1) {
      for (size_t spin = SpinStartWrite; spin <= SpinEndWrite; spin += spin) {
        if fastpath(tryAcquireWrite()) return;
        for (size_t i = 0; i < spin; i += 1) Pause();
      }
    }
    SYSCALL(pthread_rwlock_wrlock(&rwlock));
  }
  bool acquireWrite(const Time& timeout) {
    return TRY_SYSCALL(pthread_rwlock_timedwrlock(&rwlock, &timeout), ETIMEDOUT) == 0;
  }
  void release() {
    SYSCALL(pthread_rwlock_unlock(&rwlock));
  }
};

class OsSemaphore {
  sem_t sem;
public:
  OsSemaphore(size_t c = 0) {
    SYSCALL(sem_init(&sem, 0, c));
  }
  ~OsSemaphore() {
    SYSCALL(sem_destroy(&sem));
  }
  bool empty() {
    int val;
    SYSCALL(sem_getvalue(&sem, &val));
    return val >= 0;
  }
  bool open() {
    int val;
    SYSCALL(sem_getvalue(&sem, &val));
    return val > 0;
  }
  bool tryP() {
    for (;;) {
      int ret = sem_trywait(&sem);
      if (ret == 0) return true;
      else if (_SysErrno() == EAGAIN) return false;
      else { RASSERT(_SysErrno() == EINTR, _SysErrno()); }
    }
  }
  bool P(bool wait = true) {
    if (!wait) return tryP();
    while (sem_wait(&sem) < 0) { RASSERT(_SysErrno() == EINTR, _SysErrno()); }
    return true;
  }
  bool P(const Time& timeout) {
    for (;;) {
      int ret = sem_timedwait(&sem, &timeout);
      if (ret == 0) return true;
      else if (_SysErrno() == ETIMEDOUT) return false;
      else { RASSERT(_SysErrno() == EINTR, _SysErrno()); }
    }
  }
  void V() {
    SYSCALL(sem_post(&sem));
  }
};

#if TESTING_LOCK_SPIN
typedef OsLock<4,TESTING_LOCK_SPIN,1> InternalLock;
#else
typedef OsLock<0,0,0> InternalLock;
#endif

#endif /* _OsLocks_h_ */
