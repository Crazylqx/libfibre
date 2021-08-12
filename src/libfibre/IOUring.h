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
#ifndef _IOUring_h_
#define _IOUring_h_ 1

#include "runtime/BlockingSync.h"

#include <liburing.h>
#include <sys/eventfd.h>

class IOUring {
  int haltFD;
  struct io_uring ring;
  static const int NumEntries = 4096;

  IOUringStats* stats;

  struct Block {
    Fibre* fibre;
    int retcode;
  };

  template<class... Args>
  void submit(Block* b, void (*prepfunc)(struct io_uring_sqe *sqe, Args...), Args... a) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    RASSERT0(sqe);
    prepfunc(sqe, a...);
    io_uring_sqe_set_data(sqe, b);
    SYSCALL_EQ(io_uring_submit(&ring), 1);
  }

public:
  IOUring(cptr_t parent, const char* n) {
    stats = new IOUringStats(this, parent, n);
    haltFD = SYSCALLIO(eventfd(0, EFD_CLOEXEC));
    SYSCALLIO(io_uring_queue_init(NumEntries, &ring, 0));
  }
  ~IOUring() {
    io_uring_queue_exit(&ring);
    SYSCALL(close(haltFD));
  }
  size_t poll(_friend<Cluster>) {
    size_t evcnt = 0;
    struct io_uring_cqe* cqe;
    while (io_uring_peek_cqe(&ring, &cqe) == 0) {
      evcnt += 1;
      Block* b = (Block*)io_uring_cqe_get_data(cqe);
      RASSERT0(b);
      b->retcode = cqe->res;
      b->fibre->resume();
      io_uring_cqe_seen(&ring, cqe);
    }
    return evcnt;
  }
  void suspend(_friend<Cluster> fc) {
    uint64_t count;
    submit(nullptr, io_uring_prep_read, haltFD, (void*)&count, (unsigned)sizeof(count), (off_t)0);
    struct io_uring_cqe* cqe;
    size_t evcnt = 0;
    for (;;) {
      while (TRY_SYSCALL(io_uring_wait_cqe(&ring, &cqe), EINTR) < 0);
      evcnt += 1;
      Block* b = (Block*)io_uring_cqe_get_data(cqe);
      if (!b) break;
      b->retcode = cqe->res;
      b->fibre->resume();
      io_uring_cqe_seen(&ring, cqe);
    }
    io_uring_cqe_seen(&ring, cqe); // cqe for eventfd
    RASSERT(count == 1, count);
    evcnt += poll(fc); // process all available completions
    stats->events.count(evcnt);
  }
  void resume(_friend<Cluster>) {
    uint64_t val = 1;
    SYSCALL_EQ(write(haltFD, &val, sizeof(val)), sizeof(val));
  }

  template<class... Args>
  int syncIO( void (*prepfunc)(struct io_uring_sqe *sqe, Args...), Args... a) {
    Block b = { CurrFibre(), 0 };
    RuntimeDisablePreemption();
    submit(&b, prepfunc, a...);
    Suspender::suspend<false>(*b.fibre);
    return b.retcode;
  }
};

#endif /* _IOUring_h_ */
