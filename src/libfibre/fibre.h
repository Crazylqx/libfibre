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
#ifndef _fibre_h_
#define _fibre_h_ 1

#ifndef __LIBFIBRE__
#define __LIBFIBRE__ 1
#endif

#include "libfibre/Fibre.h"
#include "libfibre/EventScope.h"

typedef Fibre*               fibre_t;
typedef FibreSemaphore       fibre_sem_t;
typedef FibreBinarySemaphore fibre_binsem_t;
typedef FibreMutex           fibre_mutex_t;
typedef FibreCondition       fibre_cond_t;
typedef FibreLockRW          fibre_rwlock_t;
typedef FibreBarrier         fibre_barrier_t;

struct fibre_attr_t {
  size_t stackSize;
  bool detached;
  bool background;
  FibreCluster* cluster;
  void init() {
    stackSize = defaultStackSize;
    detached = false;
    background = false;
    cluster = &CurrCluster();
  }
};

typedef void fibre_mutexattr_t;
typedef void fibre_condattr_t;
typedef void fibre_rwlockattr_t;
typedef void fibre_barrierattr_t;

#ifdef __GNUC__
#define restrict __restrict__
#else
#define restrict
#endif

static inline int fibre_attr_init(fibre_attr_t *attr) {
  attr->init();
  return 0;
}

static inline int fibre_attr_destroy(fibre_attr_t *attr) {
  return 0;
}

static inline int fibre_attr_setstacksize(fibre_attr_t *attr, size_t stacksize) {
  attr->stackSize = stacksize;
  return 0;
}

static inline int fibre_attr_getstacksize(const fibre_attr_t *attr, size_t *stacksize) {
  *stacksize = attr->stackSize;
  return 0;
}

static inline int fibre_attr_setdetachstate(fibre_attr_t *attr, int detachstate) {
  attr->detached = detachstate;
  return 0;
}

static inline int fibre_attr_getdetachstate(const fibre_attr_t *attr, int *detachstate) {
  *detachstate = attr->detached;
  return 0;
}

static inline int fibre_attr_setbackground(fibre_attr_t *attr, int background) {
  attr->background = background;
  return 0;
}

static inline int fibre_attr_getbackground(const fibre_attr_t *attr, int *background) {
  *background = attr->background;
  return 0;
}

static inline int fibre_attr_setcluster(fibre_attr_t *attr, FibreCluster* cluster) {
  attr->cluster = cluster;
  return 0;
}

static inline int fibre_attr_getcluster(const fibre_attr_t *attr, FibreCluster* *cluster) {
  *cluster = attr->cluster;
  return 0;
}

static inline int fibre_create(fibre_t *thread, const fibre_attr_t *attr, void *(*start_routine) (void *), void *arg) {
  Fibre* f;
  if (!attr) {
    f = new Fibre;
  } else {
    f = new Fibre(*attr->cluster, attr->stackSize, attr->background);
    if (attr->detached) f->detach();
  }
  *thread = f->run(start_routine, arg);
  return 0;
}

static inline int fibre_join(fibre_t thread, void **retval) {
  RASSERT0(retval == nullptr);
  delete thread;
  return 0;
}

static inline fibre_t fibre_self(void) {
  return CurrFibre();
}

static inline int fibre_yield(void) {
  Fibre::yield();
  return 0;
}

static inline int fibre_migrate(FibreCluster* c) {
  RASSERT0(c);
  Fibre::migrateNow(*c);
  return 0;
}

static inline int fibre_sem_destroy(fibre_sem_t *sem) {
  sem->reset();
  return 0;
}

static inline int fibre_sem_init(fibre_sem_t *sem, int pshared, unsigned int value) {
  RASSERT0(pshared == 0);
  sem->reset(value);
  return 0;
}

static inline int fibre_sem_wait(fibre_sem_t *sem) {
  sem->P();
  return 0;
}

static inline int fibre_sem_trywait(fibre_sem_t *sem) {
  return sem->tryP() ? 0 : EAGAIN;
}

static inline int fibre_sem_timedwait(fibre_sem_t *sem, const struct timespec *abs_timeout) {
  return sem->P(*abs_timeout) ? 0 : ETIMEDOUT;
}

static inline int fibre_sem_post(fibre_sem_t *sem) {
  sem->V();
  return 0;
}

static inline int fibre_sem_getvalue(fibre_sem_t *sem, int *sval) {
  *sval = sem->getValue();
  return 0;
}

static inline int fibre_binsem_destroy(fibre_binsem_t *binsem) {
  binsem->reset();
  return 0;
}

static inline int fibre_binsem_init(fibre_binsem_t *binsem, int pshared, unsigned int value) {
  RASSERT0(pshared == 0);
  binsem->reset(value);
  return 0;
}

static inline int fibre_binsem_wait(fibre_binsem_t *binsem) {
  binsem->P();
  return 0;
}

static inline int fibre_binsem_trywait(fibre_binsem_t *binsem) {
  return binsem->tryP() ? 0 : EAGAIN;
}

static inline int fibre_binsem_timedwait(fibre_binsem_t *binsem, const struct timespec *abs_timeout) {
  return binsem->P(*abs_timeout) ? 0 : ETIMEDOUT;
}

static inline int fibre_binsem_post(fibre_binsem_t *binsem) {
  binsem->V();
  return 0;
}

static inline int fibre_binsem_getvalue(fibre_binsem_t *binsem, int *sval) {
  *sval = binsem->getValue();
  return 0;
}

static inline int fibre_mutex_destroy(fibre_mutex_t *mutex) {
  return 0;
}

static inline int fibre_mutex_init(fibre_mutex_t *restrict mutex, const fibre_mutexattr_t *restrict attr) {
  RASSERT0(attr == nullptr);
  return 0;
}

static inline int fibre_mutex_lock(fibre_mutex_t *mutex) {
  mutex->acquire();
  return 0;
}

static inline int fibre_mutex_trylock(fibre_mutex_t *mutex) {
  return mutex->tryAcquire() ? 0 : EBUSY;
}

static inline int fibre_mutex_timedlock(fibre_mutex_t *restrict mutex, const struct timespec *restrict abstime) {
  return mutex->acquire(*abstime) ? 0 : ETIMEDOUT;
}

static inline int fibre_mutex_unlock(fibre_mutex_t *mutex) {
  mutex->release();
  return 0;
}

static inline int fibre_cond_destroy(fibre_cond_t *cond) {
  return 0;
}

static inline int fibre_cond_init(fibre_cond_t *restrict cond, const fibre_condattr_t *restrict attr) {
  RASSERT0(attr == nullptr);
  return 0;
}

static inline int fibre_cond_wait(fibre_cond_t *restrict cond, fibre_mutex_t *restrict mutex) {
  cond->wait(*mutex);
  mutex->acquire();
  return 0;
}

static inline int fibre_cond_timedwait(fibre_cond_t *restrict cond, fibre_mutex_t *restrict mutex, const struct timespec *restrict abstime) {
  int retcode = cond->wait(*mutex, *abstime) ? 0 : ETIMEDOUT;
  mutex->acquire();
  return retcode;
}

static inline int fibre_cond_signal(fibre_cond_t *cond) {
  cond->signal();
  return 0;
}

static inline int fibre_cond_broadcast(fibre_cond_t *cond) {
  cond->signal<true>();
  return 0;
}

static inline int fibre_rwlock_destroy(fibre_rwlock_t *rwlock) {
  return 0;
}

static inline int fibre_rwlock_init(fibre_rwlock_t *restrict rwlock, const fibre_rwlockattr_t *restrict attr) {
  RASSERT0(attr == nullptr);
  return 0;
}

static inline int fibre_rwlock_tryrdlock(fibre_rwlock_t *rwlock){
  return rwlock->tryAcquireRead() ? 0 : EBUSY;
}

static inline int fibre_rwlock_rdlock(fibre_rwlock_t *rwlock){
  rwlock->acquireRead();
  return 0;
}

static inline int fibre_rwlock_timedrdlock(fibre_rwlock_t *restrict rwlock, const struct timespec *restrict abstime){
  return rwlock->acquireRead(*abstime) ? 0 : ETIMEDOUT;
}

static inline int fibre_rwlock_trywrlock(fibre_rwlock_t *rwlock){
  return rwlock->tryAcquireWrite() ? 0 : EBUSY;
}

static inline int fibre_rwlock_wrlock(fibre_rwlock_t *rwlock){
  rwlock->acquireWrite();
  return 0;
}

static inline int fibre_rwlock_timedwrlock(fibre_rwlock_t *restrict rwlock, const struct timespec *restrict abstime){
  return rwlock->acquireWrite(*abstime) ? 0 : ETIMEDOUT;
}

static inline int fibre_rwlock_unlock(fibre_rwlock_t *rwlock){
  rwlock->release();
  return 0;
}

static inline int fibre_barrier_destroy(fibre_barrier_t *barrier) {
  return 0;
}

static inline int fibre_barrier_init(fibre_barrier_t *restrict barrier, const fibre_barrierattr_t *restrict attr, unsigned count) {
  RASSERT0(attr == nullptr);
  barrier->reset(count);
  return 0;
}

static inline int fibre_barrier_wait(fibre_barrier_t *barrier) {
  return barrier->wait() ? PTHREAD_BARRIER_SERIAL_THREAD : 0;
}

#endif /* _fibre_h_ */
