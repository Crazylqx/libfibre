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
#include "libfibre/fibre.h"
#include "libfibre/cfibre.h"

struct _cfibre_t         : public Fibre {};
struct _cfibre_sem_t     : public fibre_sem_t {};
struct _cfibre_mutex_t   : public fibre_mutex_t {};
struct _cfibre_cond_t    : public fibre_cond_t {};
struct _cfibre_rwlock_t  : public fibre_rwlock_t {};
struct _cfibre_barrier_t : public fibre_barrier_t {};
struct _cfast_mutex_t    : public fast_mutex_t {};
struct _cfast_cond_t    : public fast_cond_t {};

struct _cfibre_attr_t        : public fibre_attr_t {};
struct _cfibre_mutexattr_t   : public fibre_mutexattr_t {};
struct _cfibre_condattr_t    : public fibre_condattr_t {};
struct _cfibre_rwlockattr_t  : public fibre_rwlockattr_t {};
struct _cfibre_barrierattr_t : public fibre_barrierattr_t {};
struct _cfast_mutexattr_t    : public fast_mutexattr_t {};
struct _cfast_condattr_t    : public fast_condattr_t {};

struct _cfibre_cluster_t : public Cluster {};
struct _cfibre_sproc_t   : public OsProcessor {
  _cfibre_sproc_t(Cluster& c, funcvoid1_t func = nullptr, ptr_t arg = nullptr) : OsProcessor(c, func, arg) {}
};

extern "C" int cfibre_cluster_create(cfibre_cluster_t* cluster) {
  *cluster = new _cfibre_cluster_t;
  return 0;
}

extern "C" int cfibre_cluster_destroy(cfibre_cluster_t* cluster) {
  delete *cluster;
  *cluster = nullptr;
  return 0;
}

extern "C" cfibre_cluster_t cfibre_cluster_self() {
  return &reinterpret_cast<_cfibre_cluster_t&>(Context::CurrCluster());
}

extern "C" int cfibre_pause() {
  Context::CurrCluster().pause();
  return 0;
}

extern "C" int cfibre_resume() {
  Context::CurrCluster().resume();
  return 0;
}

extern "C" int cfibre_pause_cluster(cfibre_cluster_t* cluster) {
  (*cluster)->pause();
  return 0;
}

extern "C" int cfibre_resume_cluster(cfibre_cluster_t* cluster) {
  (*cluster)->resume();
  return 0;
}

extern "C" int cfibre_sproc_create(cfibre_sproc_t* sproc, cfibre_cluster_t cluster) {
  if (cluster == nullptr) cfibre_cluster_self();
  *sproc = new _cfibre_sproc_t(*cluster);
  return 0;
}

extern "C" int cfibre_sproc_create_init(cfibre_sproc_t* sproc, cfibre_cluster_t cluster, void (*func)(void *), void *arg) {
  if (cluster == nullptr) cfibre_cluster_self();
  *sproc = new _cfibre_sproc_t(*cluster, func, arg);
  return 0;
}

extern "C" int cfibre_sproc_destroy(cfibre_sproc_t* sproc) {
  delete *sproc;
  *sproc = nullptr;
  return 0;
}

extern "C" pthread_t cfibre_sproc_pthread(cfibre_sproc_t sproc) {
  return sproc->getSysID();
}

extern "C" int cfibre_attr_init(cfibre_attr_t *attr) {
  *attr = new _cfibre_attr_t;
  return fibre_attr_init(*attr);
}

extern "C" int cfibre_attr_destroy(cfibre_attr_t *attr) {
  int ret = fibre_attr_destroy(*attr);
  delete *attr;
  *attr = nullptr;
  return ret;
}

extern "C" int cfibre_get_errno() {
  return _SysErrno();
}

extern "C" void cfibre_set_errno(int e) {
  _SysErrnoSet() = e;
}

extern "C" int cfibre_attr_setstacksize(cfibre_attr_t *attr, size_t stacksize) {
  return fibre_attr_setstacksize(*attr, stacksize);
}

extern "C" int cfibre_attr_getstacksize(const cfibre_attr_t *attr, size_t *stacksize) {
  return fibre_attr_getstacksize(*attr, stacksize);
}

extern "C" int cfibre_attr_setdetachstate(cfibre_attr_t *attr, int detachstate) {
  return fibre_attr_setdetachstate(*attr, detachstate);
}

extern "C" int cfibre_attr_getdetachstate(const cfibre_attr_t *attr, int *detachstate) {
  return fibre_attr_getdetachstate(*attr, detachstate);
}

extern "C" int cfibre_attr_setbackground(cfibre_attr_t *attr, int background) {
  return fibre_attr_setbackground(*attr, background);
}

extern "C" int cfibre_attr_getbackground(const cfibre_attr_t *attr, int *background) {
  return fibre_attr_getbackground(*attr, background);
}

extern "C" int cfibre_attr_setcluster(cfibre_attr_t *attr, cfibre_cluster_t cluster) {
  return fibre_attr_setcluster(*attr, cluster);
}

extern "C" int cfibre_attr_getcluster(const cfibre_attr_t *attr, cfibre_cluster_t *cluster) {
  return fibre_attr_getcluster(*attr, (Cluster**)cluster);
}

extern "C" int cfibre_create(cfibre_t *thread, const cfibre_attr_t *attr, void *(*start_routine) (void *), void *arg) {
  if (attr) {
    return fibre_create((fibre_t*)thread, (fibre_attr_t*)*attr, start_routine, arg);
  } else {
    return fibre_create((fibre_t*)thread, nullptr, start_routine, arg);
  }
}

extern "C" int cfibre_join(cfibre_t thread, void **retval) {
  return fibre_join(thread, retval);
}

extern "C" cfibre_t cfibre_self(void) {
  return (cfibre_t)fibre_self();
}

extern "C" int cfibre_yield(void) {
  return fibre_yield();
}

extern "C" int cfibre_migrate(cfibre_cluster_t cluster) {
  return fibre_migrate(cluster);
}

extern "C" int cfibre_sem_init(cfibre_sem_t *sem, int pshared, unsigned int value) {
  *sem = (cfibre_sem_t)new fibre_sem_t;
  return fibre_sem_init(*sem, pshared, value);
}

extern "C" int cfibre_sem_destroy(cfibre_sem_t *sem) {
  int ret = fibre_sem_destroy(*sem);
  delete *sem;
  *sem = nullptr;
  return ret;
}

extern "C" int cfibre_sem_wait(cfibre_sem_t *sem) {
  return fibre_sem_wait(*sem);
}

extern "C" int cfibre_sem_trywait(cfibre_sem_t *sem) {
  return fibre_sem_trywait(*sem);
}

extern "C" int cfibre_sem_timedwait(cfibre_sem_t *sem, const struct timespec *abs_timeout) {
  return fibre_sem_timedwait(*sem, abs_timeout);
}

extern "C" int cfibre_sem_post(cfibre_sem_t *sem) {
  return fibre_sem_post(*sem);
}

extern "C" int cfibre_sem_getvalue(cfibre_sem_t *sem, int *sval) {
  return fibre_sem_getvalue(*sem, sval);
}

extern "C" int cfibre_mutex_init(cfibre_mutex_t *restrict mutex, const cfibre_mutexattr_t *restrict attr) {
  *mutex = (cfibre_mutex_t)new fibre_mutex_t;
  return fibre_mutex_init(*mutex, (fibre_mutexattr_t*)attr);
}

extern "C" int cfibre_mutex_destroy(cfibre_mutex_t *mutex) {
  int ret = fibre_mutex_destroy(*mutex);
  delete *mutex;
  *mutex = nullptr;
  return ret;
}

extern "C" int cfibre_mutex_lock(cfibre_mutex_t *mutex) {
  return fibre_mutex_lock(*mutex);
}

extern "C" int cfibre_mutex_trylock(cfibre_mutex_t *mutex) {
  return fibre_mutex_trylock(*mutex);
}

extern "C" int cfibre_mutex_timedlock(cfibre_mutex_t *restrict mutex, const struct timespec *restrict abstime) {
  return fibre_mutex_timedlock(*mutex, abstime);
}

extern "C" int cfibre_mutex_unlock(cfibre_mutex_t *mutex) {
  return fibre_mutex_unlock(*mutex);
}

extern "C" int cfibre_cond_init(cfibre_cond_t *restrict cond, const cfibre_condattr_t *restrict attr) {
  *cond = (cfibre_cond_t)new fibre_cond_t;
  return fibre_cond_init(*cond, (fibre_condattr_t*)attr);
}

extern "C" int cfibre_cond_destroy(cfibre_cond_t *cond) {
  int ret = fibre_cond_destroy(*cond);
  delete *cond;
  *cond = nullptr;
  return ret;
}

extern "C" int cfibre_cond_wait(cfibre_cond_t *restrict cond, cfibre_mutex_t *restrict mutex) {
  return fibre_cond_wait(*cond, *mutex);
}

extern "C" int cfibre_cond_timedwait(cfibre_cond_t *restrict cond, cfibre_mutex_t *restrict mutex, const struct timespec *restrict abstime) {
  return fibre_cond_timedwait(*cond, *mutex, abstime);
}

extern "C" int cfibre_cond_signal(cfibre_cond_t *cond) {
  return fibre_cond_signal(*cond);
}

extern "C" int cfibre_cond_broadcast(cfibre_cond_t *cond) {
  return fibre_cond_broadcast(*cond);
}

extern "C" int cfibre_rwlock_init(cfibre_rwlock_t *restrict rwlock, const cfibre_rwlockattr_t *restrict attr) {
  *rwlock = (cfibre_rwlock_t)new fibre_rwlock_t;
  return fibre_rwlock_init(*rwlock, (fibre_rwlockattr_t*)attr);
}

extern "C" int cfibre_rwlock_destroy(cfibre_rwlock_t *rwlock) {
  int ret = fibre_rwlock_destroy(*rwlock);
  delete *rwlock;
  *rwlock = nullptr;
  return ret;
}

extern "C" int cfibre_rwlock_rdlock(cfibre_rwlock_t *rwlock) {
  return fibre_rwlock_rdlock(*rwlock);
}

extern "C" int cfibre_rwlock_tryrdlock(cfibre_rwlock_t *rwlock) {
  return fibre_rwlock_tryrdlock(*rwlock);
}

extern "C" int cfibre_rwlock_timedrdlock(cfibre_rwlock_t *restrict rwlock, const struct timespec *restrict abstime) {
  return fibre_rwlock_timedrdlock(*rwlock, abstime);
}

extern "C" int cfibre_rwlock_wrlock(cfibre_rwlock_t *rwlock) {
  return fibre_rwlock_wrlock(*rwlock);
}

extern "C" int cfibre_rwlock_trywrlock(cfibre_rwlock_t *rwlock) {
  return fibre_rwlock_trywrlock(*rwlock);
}

extern "C" int cfibre_rwlock_timedwrlock(cfibre_rwlock_t *restrict rwlock, const struct timespec *restrict abstime) {
  return fibre_rwlock_timedwrlock(*rwlock, abstime);
}

extern "C" int cfibre_rwlock_unlock(cfibre_rwlock_t *rwlock) {
  return fibre_rwlock_unlock(*rwlock);
}

extern "C" int cfibre_barrier_init(cfibre_barrier_t *restrict barrier, const cfibre_barrierattr_t *restrict attr, unsigned count) {
  *barrier = (cfibre_barrier_t)new fibre_barrier_t;
  return fibre_barrier_init(*barrier, (fibre_barrierattr_t*)attr, count);
}

extern "C" int cfibre_barrier_destroy(cfibre_barrier_t *barrier) {
  int ret = fibre_barrier_destroy(*barrier);
  delete *barrier;
  *barrier = nullptr;
  return ret;
}

extern "C" int cfibre_barrier_wait(cfibre_barrier_t *barrier) {
  return fibre_barrier_wait(*barrier);
}

extern "C" int cfast_mutex_init(cfast_mutex_t *restrict mutex, const cfast_mutexattr_t *restrict attr) {
  *mutex = (cfast_mutex_t)new fast_mutex_t;
  return fast_mutex_init(*mutex, (fast_mutexattr_t*)attr);
}

extern "C" int cfast_mutex_destroy(cfast_mutex_t *mutex) {
  int ret = fast_mutex_destroy(*mutex);
  delete *mutex;
  *mutex = nullptr;
  return ret;
}

extern "C" int cfast_mutex_lock(cfast_mutex_t *mutex) {
  return fast_mutex_lock(*mutex);
}

extern "C" int cfast_mutex_trylock(cfast_mutex_t *mutex) {
  return fast_mutex_trylock(*mutex);
}

extern "C" int cfast_mutex_unlock(cfast_mutex_t *mutex) {
  return fast_mutex_unlock(*mutex);
}

extern "C" int cfast_cond_init(cfast_cond_t *restrict cond, const cfast_condattr_t *restrict attr) {
  *cond = (cfast_cond_t)new fast_cond_t;
  return fast_cond_init(*cond, (fast_condattr_t*)attr);
}

extern "C" int cfast_cond_destroy(cfast_cond_t *cond) {
  int ret = fast_cond_destroy(*cond);
  delete *cond;
  *cond = nullptr;
  return ret;
}

extern "C" int cfast_cond_wait(cfast_cond_t *restrict cond, cfast_mutex_t *restrict mutex) {
  return fast_cond_wait(*cond, *mutex);
}

extern "C" int cfast_cond_timedwait(cfast_cond_t *restrict cond, cfast_mutex_t *restrict mutex, const struct timespec *restrict abstime) {
  return fast_cond_timedwait(*cond, *mutex, abstime);
}

extern "C" int cfast_cond_signal(cfast_cond_t *cond) {
  return fast_cond_signal(*cond);
}

extern "C" int cfast_cond_broadcast(cfast_cond_t *cond) {
  return fast_cond_broadcast(*cond);
}

extern "C" int cfibre_socket(int domain, int type, int protocol) {
  return lfSocket(domain, type, protocol);
}

extern "C" int cfibre_bind(int socket, const struct sockaddr *address, socklen_t address_len) {
  return lfBind(socket, address, address_len);
}

extern "C" int cfibre_listen(int socket, int backlog) {
  return lfListen(socket, backlog);
}

extern "C" int cfibre_accept(int socket, struct sockaddr *restrict address, socklen_t *restrict address_len) {
  return lfAccept(socket, address, address_len, 0);
}

extern "C" int cfibre_accept4(int socket, struct sockaddr *restrict address, socklen_t *restrict address_len, int flags) {
  return lfAccept(socket, address, address_len, flags);
}

extern "C" int cfibre_connect(int socket, const struct sockaddr *address, socklen_t address_len) {
  return lfConnect(socket, address, address_len);
}

extern "C" int cfibre_dup(int fd) {
  return lfDup(fd);
}

extern "C" int cfibre_close(int fd) {
  return lfClose(fd);
}

extern "C" ssize_t cfibre_send(int socket, const void *buffer, size_t length, int flags) {
  return lfOutput(send, socket, buffer, length, flags);
}

extern "C" ssize_t cfibre_sendto(int socket, const void *message, size_t length, int flags, const struct sockaddr *dest_addr, socklen_t dest_len) {
  return lfOutput(sendto, socket, message, length, flags, dest_addr, dest_len);
}

extern "C" ssize_t cfibre_sendmsg(int socket, const struct msghdr *message, int flags) {
  return lfOutput(sendmsg, socket, message, flags);
}

extern "C" ssize_t cfibre_write(int fildes, const void *buf, size_t nbyte) {
  return lfOutput(write, fildes, buf, nbyte);
}

extern "C" ssize_t cfibre_recv(int socket, void *buffer, size_t length, int flags) {
  return lfInput(recv, socket, buffer, length, flags);
}

extern "C" ssize_t cfibre_recvfrom(int socket, void *restrict buffer, size_t length, int flags, struct sockaddr *restrict address, socklen_t *restrict address_len) {
  return lfInput(recvfrom, socket, buffer, length, flags, address, address_len);
}

extern "C" ssize_t cfibre_recvmsg(int socket, struct msghdr *message, int flags) {
  return lfInput(recvmsg, socket, message, flags);
}

extern "C" ssize_t cfibre_read(int fildes, void *buf, size_t nbyte) {
  return lfInput(read, fildes, buf, nbyte);
}

extern "C" void cfibre_suspendFD(int fd) {
  Context::CurrEventScope().suspendFD(fd);
}

extern "C" void cfibre_resumeFD(int fd) {
  Context::CurrEventScope().resumeFD(fd);
}

extern "C" int cfibre_usleep(useconds_t usec) {
  Fibre::usleep(usec);
  return 0;
}

extern "C" int cfibre_sleep(unsigned int seconds) {
  Fibre::sleep(seconds);
  return 0;
}
