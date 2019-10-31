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
#ifndef _cfibre_h_
#define _cfibre_h_ 1

#include <unistd.h>     // useconds_t
#include <time.h>       // struct timespec
#include <sys/socket.h> // socket types
#include <pthread.h>

struct _cfibre_t;
struct _cfibre_sem_t;
struct _cfibre_binsem_t;
struct _cfibre_mutex_t;
struct _cfibre_cond_t;
struct _cfibre_rwlock_t;
struct _cfibre_barrier_t;
struct _cfibre_sproc_t;

typedef struct _cfibre_t*         cfibre_t;
typedef struct _cfibre_sem_t*     cfibre_sem_t;
typedef struct _cfibre_binsem_t*  cfibre_binsem_t;
typedef struct _cfibre_mutex_t*   cfibre_mutex_t;
typedef struct _cfibre_cond_t*    cfibre_cond_t;
typedef struct _cfibre_rwlock_t*  cfibre_rwlock_t;
typedef struct _cfibre_barrier_t* cfibre_barrier_t;
typedef struct _cfibre_cluster_t* cfibre_cluster_t;
typedef struct _cfibre_sproc_t*   cfibre_sproc_t;

typedef struct _cfibre_attr_t*    cfibre_attr_t;

typedef int cfibre_mutexattr_t;
typedef int cfibre_condattr_t;
typedef int cfibre_rwlockattr_t;
typedef int cfibre_barrierattr_t;

#ifdef __cplusplus
extern "C" {
#endif

int cfibre_cluster_create(cfibre_cluster_t* cluster);
int cfibre_cluster_destroy(cfibre_cluster_t* cluster);
cfibre_cluster_t cfibre_cluster_self(void);

int cfibre_errno(void);
int* cfibre_errno_set(void);

int cfibre_pause(void);
int cfibre_resume(void);
int cfibre_pause_cluster(cfibre_cluster_t* cluster);
int cfibre_resume_cluster(cfibre_cluster_t* cluster);

int cfibre_sproc_prepare(cfibre_sproc_t* sp);
int cfibre_sproc_prepare_cluster(cfibre_sproc_t* sp, cfibre_cluster_t cluster);
int cfibre_sproc_create(cfibre_sproc_t* sp);
int cfibre_sproc_create_init(cfibre_sproc_t* sp, void (*func)(void *), void *arg);
int cfibre_sproc_destroy(cfibre_sproc_t* sp);
pthread_t cfibre_sproc_pthread(cfibre_sproc_t sp);

int cfibre_attr_init(cfibre_attr_t *attr);
int cfibre_attr_destroy(cfibre_attr_t *attr);
int cfibre_attr_setstacksize(cfibre_attr_t *attr, size_t stacksize);
int cfibre_attr_getstacksize(const cfibre_attr_t *attr, size_t *stacksize);
int cfibre_attr_setdetachstate(cfibre_attr_t *attr, int detachstate);
int cfibre_attr_getdetachstate(const cfibre_attr_t *attr, int *detachstate);
int cfibre_attr_setbackground(cfibre_attr_t *attr, int background);
int cfibre_attr_getbackground(const cfibre_attr_t *attr, int *background);
int cfibre_attr_setcluster(cfibre_attr_t *attr, cfibre_cluster_t cluster);
int cfibre_attr_getcluster(const cfibre_attr_t *attr, cfibre_cluster_t *cluster);

int cfibre_create(cfibre_t *thread, const cfibre_attr_t *attr, void *(*start_routine) (void *), void *arg);
int cfibre_join(cfibre_t thread, void **retval);
cfibre_t cfibre_self(void);
int cfibre_yield(void);
int cfibre_migrate(cfibre_cluster_t cluster);

int cfibre_sem_destroy(cfibre_sem_t *sem);
int cfibre_sem_init(cfibre_sem_t *sem, int pshared, unsigned int value);
int cfibre_sem_wait(cfibre_sem_t *sem);
int cfibre_sem_trywait(cfibre_sem_t *sem);
int cfibre_sem_timedwait(cfibre_sem_t *sem, const struct timespec *abs_timeout);
int cfibre_sem_post(cfibre_sem_t *sem);
int cfibre_sem_getvalue(cfibre_sem_t *sem, int *sval);

int cfibre_binsem_destroy(cfibre_binsem_t *binsem);
int cfibre_binsem_init(cfibre_binsem_t *binsem, int pshared, unsigned int value);
int cfibre_binsem_wait(cfibre_binsem_t *binsem);
int cfibre_binsem_trywait(cfibre_binsem_t *binsem);
int cfibre_binsem_timedwait(cfibre_binsem_t *binsem, const struct timespec *abs_timeout);
int cfibre_binsem_post(cfibre_binsem_t *binsem);
int cfibre_binsem_getvalue(cfibre_binsem_t *binsem, int *sval);

int cfibre_mutex_destroy(cfibre_mutex_t *mutex);
int cfibre_mutex_init(cfibre_mutex_t *restrict mutex, const cfibre_mutexattr_t *restrict attr);
int cfibre_mutex_lock(cfibre_mutex_t *mutex);
int cfibre_mutex_trylock(cfibre_mutex_t *mutex);
int cfibre_mutex_timedlock(cfibre_mutex_t *restrict mutex, const struct timespec *restrict abstime);
int cfibre_mutex_unlock(cfibre_mutex_t *mutex);

int cfibre_cond_destroy(cfibre_cond_t *cond);
int cfibre_cond_init(cfibre_cond_t *restrict cond, const cfibre_condattr_t *restrict attr);
int cfibre_cond_wait(cfibre_cond_t *restrict cond, cfibre_mutex_t *restrict mutex);
int cfibre_cond_timedwait(cfibre_cond_t *restrict cond, cfibre_mutex_t *restrict mutex, const struct timespec *restrict abstime);
int cfibre_cond_signal(cfibre_cond_t *cond);
int cfibre_cond_broadcast(cfibre_cond_t *cond);

int cfibre_rwlock_destroy(cfibre_rwlock_t *rwlock);
int cfibre_rwlock_init(cfibre_rwlock_t *restrict rwlock, const cfibre_rwlockattr_t *restrict attr);
int cfibre_rwlock_rdlock(cfibre_rwlock_t *rwlock);
int cfibre_rwlock_tryrdlock(cfibre_rwlock_t *rwlock);
int cfibre_rwlock_timedrdlock(cfibre_rwlock_t *restrict rwlock, const struct timespec *restrict abstime);
int cfibre_rwlock_wrlock(cfibre_rwlock_t *rwlock);
int cfibre_rwlock_trywrlock(cfibre_rwlock_t *rwlock);
int cfibre_rwlock_timedwrlock(cfibre_rwlock_t *restrict rwlock, const struct timespec *restrict abstime);
int cfibre_rwlock_unlock(cfibre_rwlock_t *rwlock);

int cfibre_barrier_destroy(cfibre_barrier_t *barrier);
int cfibre_barrier_init(cfibre_barrier_t *restrict barrier, const cfibre_barrierattr_t *restrict attr, unsigned count);
int cfibre_barrier_wait(cfibre_barrier_t *barrier);

int cfibre_usleep(useconds_t uses);
int cfibre_sleep(unsigned int secs);

int cfibre_socket(int domain, int type, int protocol);
int cfibre_bind(int socket, const struct sockaddr *address, socklen_t address_len);
int cfibre_listen(int socket, int backlog);
int cfibre_accept(int socket, struct sockaddr *restrict address, socklen_t *restrict address_len);
int cfibre_accept4(int socket, struct sockaddr *restrict address, socklen_t *restrict address_len, int flags);
int cfibre_connect(int socket, const struct sockaddr *address, socklen_t address_len);
int cfibre_dup(int fildes);
int cfibre_close(int fildes);
ssize_t cfibre_send(int socket, const void *buffer, size_t length, int flags);
ssize_t cfibre_sendto(int socket, const void *message, size_t length, int flags, const struct sockaddr *dest_addr, socklen_t dest_len);
ssize_t cfibre_sendmsg(int socket, const struct msghdr *message, int flags);
ssize_t cfibre_write(int fildes, const void *buf, size_t nbyte);
ssize_t cfibre_recv(int socket, void *buffer, size_t length, int flags);
ssize_t cfibre_recvfrom(int socket, void *restrict buffer, size_t length, int flags, struct sockaddr *restrict address, socklen_t *restrict address_len);
ssize_t cfibre_recvmsg(int socket, struct msghdr *message, int flags);
ssize_t cfibre_read(int fildes, void *buf, size_t nbyte);

void cfibre_suspendFD(int fd);
void cfibre_resumeFD(int fd);

#ifdef __cplusplus
}
#endif

#endif /* _cfibre_h_ */
