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
#if defined(__cforall)

#define _GNU_SOURCE
#include <clock.hfa>
#include <math.hfa>
#include <stdio.h>
#include <stdlib.hfa>
extern "C" {
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h> // getopt
}
#define nullptr 0p

#else

#include <chrono>
#include <iostream>
#include <string>
#include <cassert>
#include <cmath>
#include <csignal>
#include <unistd.h> // getopt
using namespace std;

#endif

#if __FreeBSD__
#include <sys/cpuset.h>
#include <pthread_np.h>
typedef cpuset_t cpu_set_t;
#endif

#ifdef VARIANT

#ifndef SYSCALL
#include "syscall_macro.h"
#define SYSCALL(call)   SYSCALL_CMP(call,==,0,0)
#define SYSCALLIO(call) SYSCALL_CMP(call,>=,0,0)
#endif /* SYSCALL */

#include VARIANT
#include "runtime/Platform.h"

#else

#include "include/libfibre.h"

#endif /* VARIANT */

// configuration default settings
static unsigned int threadCount = 2;
static unsigned int duration = 10;
static unsigned int fibreCount = 4;
static unsigned int lockCount = 1;
static unsigned int work_unlocked = 10000;
static unsigned int work_locked = 10000;

static bool yieldFlag = false;
static bool serialFlag = false;
static bool affinityFlag = false;
static bool calibration = false;

static bool yieldExperiment = false;
static char lockType = 'B';

// worker descriptor
struct Worker {
  shim_thread_t* runner;
  volatile unsigned long long counter;
};

// lock descriptor
struct Lock {
  shim_mutex_t mtx;
  volatile unsigned long long counter;
};

// arrays of worker/lock descriptors
static Worker* workers = nullptr;
static Lock* locks = nullptr;

static shim_barrier_t* cbar = nullptr;
static shim_barrier_t* sbar = nullptr;

// manage experiment duration
static unsigned int ticks = 0;
static volatile bool running = false;

// alarm invoked every second
static void alarmHandler(int) {
  ticks += 1;
  if (ticks >= duration) {
    fprintf(stderr, "\r");
    fflush(stderr);
    running = false;
  } else {
    fprintf(stderr, "\r%u", ticks);
    fflush(stderr);
  }
}

// help message
static void usage(const char* prog) {
  fprintf(stderr, "usage: %s -d <duration (secs)> -f <total fibres> -l <locks> -t <system threads> -u <unlocked work> -w <locked work> -s -y -a -c -Y -L <lock type>\n", prog);
}

// command-line option processing
static bool opts(int argc, char** argv) {
  for (;;) {
    int option = getopt( argc, argv, "d:f:l:t:u:w:syacYL:h?" );
    if ( option < 0 ) break;
    switch(option) {
    case 'd': duration = atoi(optarg); break;
    case 'f': fibreCount = atoi(optarg); break;
    case 'l': lockCount = atoi(optarg); break;
    case 't': threadCount = atoi(optarg); break;
    case 'u': work_unlocked = atoi(optarg); break;
    case 'w': work_locked = atoi(optarg); break;
    case 's': serialFlag = true; break;
    case 'y': yieldFlag = true; break;
    case 'a': affinityFlag = true; break;
    case 'c': calibration = true; break;
    case 'Y': yieldExperiment = true; break;
    case 'L': lockType = optarg[0]; break;
    case 'h':
    case '?':
      usage(argv[0]);
      return false;
    default:
      fprintf(stderr, "unknown option - %c\n", (char)option);
      usage(argv[0]);
      return false;
    }
  }
  if (argc != optind) {
    fprintf(stderr, "unknown argument - %s\n", argv[optind]);
    usage(argv[0]);
    return false;
  }
  if (duration == 0 || fibreCount == 0 || lockCount == 0 || threadCount == 0) {
    fprintf(stderr, "none of -d, -f, -l, -t can be zero\n");
    usage(argv[0]);
    return false;
  }
  if (lockType >= 'a') lockType -= 32;
  switch (lockType) {
#if defined HASTRYLOCK
    case 'Y':
    case 'S':
#endif
    case 'B': break;
    default: fprintf(stderr, "lock type %c not supported\n", lockType);
    return false;
  }
#if defined MORDOR_MAIN
  if (!yieldFlag) {
    cout << "Mordor always runs with -y flag set" << endl;
    yieldFlag = true;
  }
#endif
#if defined MORDOR_MAIN || defined BOOST_VERSION || defined QTHREAD_VERSION
  if (affinityFlag) {
    cerr << "boost, mordor, and qthreads do not support affinity at this time" << endl;
    return false;
  }
#endif
#if defined ARACHNE_H_
  unsigned int maxFibreCount = Arachne::minNumCores * 55;
  if (fibreCount > maxFibreCount) fibreCount = maxFibreCount;
#endif
  return true;
}

static const int workBufferSize = 16;

static inline void dowork(volatile int* buffer, unsigned int steps) {
  int value = 0;
  for (unsigned int i = 0; i < steps; i += 1) {
    // a little more work than just a memory access helps with stability
    value += (buffer[i % workBufferSize] * 17) / 23 + 55;
  }
  buffer[0] += value;
}

#if defined(__cforall)

typedef Time mytime_t;
static inline mytime_t now() { return getTime(); }
int64_t diff_to_ns( mytime_t a, mytime_t b ) {
  return (a - b)`ns;
}

#else

typedef std::chrono::high_resolution_clock::time_point mytime_t;
static inline mytime_t now() { return std::chrono::high_resolution_clock::now(); };
int64_t diff_to_ns( mytime_t a, mytime_t b ) {
  std::chrono::nanoseconds d = a -b;
  return d.count();
}

#endif

static uint64_t timerOverhead = 0;

static void calibrateTimer() {
  mytime_t start = now();
  mytime_t tmp;
  for (unsigned int i = 0; i < (1 << 24) - 1; i += 1) {
    tmp = now();
  }
  (void)tmp;
  mytime_t end = now();
  timerOverhead = diff_to_ns(end, start) / (1 << 24);
}

static int compare(const void * lhs, const void * rhs) {
  return ((unsigned long)lhs) < ((unsigned long)rhs);
}

static unsigned int calibrateInterval(unsigned int period) {
  // set up work buffer
  int buffer[workBufferSize];
  for (int i = 0; i < workBufferSize; i += 1) buffer[i] = random() % 1024;

  unsigned int low = 1;
  unsigned int high = 2;
  unsigned int runs = (1<<28) / period;
  printf("%uns - upper bound:", period);
  for (;;) {
    printf(" %u", high);
    fflush(stdout);
    mytime_t start = now();
    for (unsigned int i = 0; i < runs; i++) dowork(buffer, high);
    mytime_t end = now();
    if ((diff_to_ns(end, start) - timerOverhead) / runs > period) break;
    high = high * 2;
  }
  printf("\nbinary search:");
  for (;;) {
    printf(" [%u:%u]", low, high);
    fflush(stdout);
    unsigned int next = (low + high) / 2;
    if (next == low) break;
    static const int SampleCount = 3;
    unsigned long samples[SampleCount];
    for (int s = 0; s < SampleCount; s += 1) {
      mytime_t start = now();
      for (unsigned int i = 0; i < runs; i++) dowork(buffer, next);
      mytime_t end = now();
      samples[s] = ( diff_to_ns(end, start) - timerOverhead) / runs;
    }
    qsort( samples, SampleCount, sizeof(unsigned long), compare );
    if (samples[SampleCount/2] > period) high = next;
    else low = next;
  }
  printf("\n");
  assert(low + 1 == high);
  return high;
}

static void yielder(void* arg) {
  // signal creation
  shim_barrier_wait(cbar);
  // wait for start signal
  shim_barrier_wait(sbar);
  unsigned int num = (uintptr_t)arg;

  unsigned long long count = 0;
  while (running) {
    shim_yield();
    count += 1;
  }
  workers[num].counter = count;
}

// worker routine
static void worker(void* arg) {
  // set up work buffer
  int buffer[workBufferSize];
  for (int i = 0; i < workBufferSize; i += 1) buffer[i] = random() % 1024;
  // initialize
  unsigned int num = (uintptr_t)arg;
  unsigned int lck = random() % lockCount;
  // signal creation
  shim_barrier_wait(cbar);
  // wait for start signal
  shim_barrier_wait(sbar);
  // run loop
  while (running) {
    // unlocked work
    if (work_unlocked != (unsigned int)-1) dowork(buffer, work_unlocked);
    // locked work and counters
    switch (lockType) {
      // regular blocking lock
      case 'B': shim_mutex_lock(&locks[lck].mtx); break;
#if defined HASTRYLOCK
      // plain spin lock
      case 'S': while (!shim_mutex_trylock(&locks[lck].mtx)) Pause(); break;
      // yield-based busy-locking (as in qthreads, boost)
      case 'Y': while (!shim_mutex_trylock(&locks[lck].mtx)) shim_yield(); break;
#endif
      default: fprintf(stderr, "internal error: lock type\n"); abort();
    }
    if (work_locked != (unsigned int)-1) dowork(buffer, work_locked);
    workers[num].counter += 1;
    locks[lck].counter += 1;
    shim_mutex_unlock(&locks[lck].mtx);
    if (yieldFlag) shim_yield();
    // pick next lock, serial or random
    if (serialFlag) lck += 1;
    else lck = random();
    lck %= lockCount;
  }
}

#if defined __U_CPLUSPLUS__
_PeriodicTask AlarmTask {
  void main() { alarmHandler(0); if (!running) return; }
public:
  AlarmTask(uDuration period) : uPeriodicBaseTask(period) {};
};
#endif

#if defined __U_CPLUSPLUS__ || defined ARACHNE_H_
#define EXITMAIN return
#else
#define EXITMAIN return 0
#endif


// main routine
#if defined ARACHNE_H_
void AppMain(int argc, char* argv[]) {
#elif defined MORDOR_MAIN
MORDOR_MAIN(int argc, char *argv[]) {
#elif defined __U_CPLUSPLUS__
void uMain::main() {
#else
int main(int argc, char* argv[]) {
#endif
  // parse command-line arguments
  if (!opts(argc, argv)) EXITMAIN;

  // print configuration
  printf("threads: %u workers: %u locks: %u", threadCount, fibreCount, lockCount);
  if (affinityFlag) printf(" affinity");
  if (serialFlag) printf(" serial");
  if (yieldFlag) printf(" yield");
  printf("\n");
  printf("duration: %u", duration);
  if (work_locked != (unsigned int)-1) printf(" locked work: %u", work_locked);
  if (work_unlocked != (unsigned int)-1) printf(" unlocked work: %u", work_unlocked);
  printf("\n");

  // set up random number generator
  srandom(time(nullptr));

  // run timer calibration, if requested
  if (calibration) {
    calibrateTimer();
    printf("time overhead: %lu\n", timerOverhead);
    unsigned int l = calibrateInterval(work_locked);
    printf("WORK: -w %u\n", l);
    unsigned int u = calibrateInterval(work_unlocked);
    printf("UNLOCKED work: -u %u\n", u);
    printf("\n");
    printf("WARNING: these numbers are not necessarily very accurate."
           " Double-check the actual runtime with 'perf'\n");
    printf("\n");
    EXITMAIN;
  }

  // create system processors (pthreads)
#if defined MORDOR_MAIN
  poolScheduler = new WorkerPool(threadCount);
#elif defined BOOST_VERSION
  boost_init(threadCount);
#elif defined __LIBFIBRE__
  FibreInit();
  CurrCluster().addWorkers(threadCount - 1);
#elif defined __U_CPLUSPLUS__
  uProcessor* proc = new uProcessor[threadCount - 1];
#elif defined QTHREAD_VERSION
  setenv("QTHREAD_STACK_SIZE", "65536", 1);
  qthread_init(threadCount);
#elif defined _FIBER_FIBER_H_
  fiber_manager_init(threadCount);
#endif

#if 0 // test deadlock behaviour
  shim_barrier_t* deadlock = shim_barrier_create(2);
  shim_barrier_wait(deadlock);
#endif

#if defined __LIBFIBRE__ || __U_CPLUSPLUS__
  if (affinityFlag) {
#if defined __LIBFIBRE__
    pthread_t* tids = (pthread_t*)calloc(sizeof(pthread_t), threadCount);
    size_t tcnt = CurrCluster().getWorkerSysIDs(tids, threadCount);
    assert(tcnt == threadCount);
#endif
    cpu_set_t onecpu, allcpus;
    CPU_ZERO(&onecpu);
    CPU_ZERO(&allcpus);
#if defined __LIBFIBRE__
    SYSCALL(pthread_getaffinity_np(pthread_self(), sizeof(allcpus), &allcpus));
#else
    uThisProcessor().getAffinity(allcpus);
#endif
    int cpu = 0;
    for (unsigned int i = 0; i < threadCount; i += 1) {
      while (!CPU_ISSET(cpu, &allcpus)) cpu = (cpu + 1) % CPU_SETSIZE;
      CPU_SET(cpu, &onecpu);
#if defined __LIBFIBRE__
      SYSCALL(pthread_setaffinity_np(tids[i], sizeof(onecpu), &onecpu));
#else
      if (i == threadCount - 1) uThisProcessor().setAffinity(onecpu);
      else proc[i].setAffinity(onecpu);
#endif
      CPU_CLR(cpu, &onecpu);
      cpu = (cpu + 1) % CPU_SETSIZE;
    }
#if defined __LIBFIBRE__
    free(tids);
#endif
  }
#endif

  // create barriers
  cbar = shim_barrier_create(fibreCount + 1);
  sbar = shim_barrier_create(fibreCount + 1);

  // create locks
  locks = (Lock*)calloc(sizeof(Lock), lockCount);
  for (unsigned int i = 0; i < lockCount; i += 1) {
    shim_mutex_init(&locks[i].mtx);
    locks[i].counter = 0;
  }

  // create threads
  workers = (Worker*)calloc(sizeof(Worker), fibreCount);
  for (unsigned int i = 0; i < fibreCount; i += 1) {
    workers[i].runner = shim_thread_create(yieldExperiment ? yielder : worker, (void*)((uintptr_t)i));
    workers[i].counter = 0;
  }

#if defined PTHREADS
  if (affinityFlag) {
    cpu_set_t allcpus;
    CPU_ZERO(&allcpus);
    cpu_set_t onecpu;
    CPU_ZERO(&onecpu);
    SYSCALL(pthread_getaffinity_np(pthread_self(), sizeof(allcpus), &allcpus));
    int cpu = 0;
    for (unsigned int i = 0; i < fibreCount; i += 1) {
      while (!CPU_ISSET(cpu, &allcpus)) cpu = (cpu + 1) % CPU_SETSIZE;
      CPU_SET(cpu, &onecpu);
      SYSCALL(pthread_setaffinity_np(*workers[i].runner, sizeof(onecpu), &onecpu));
      CPU_CLR(cpu, &onecpu);
      cpu = (cpu + 1) % CPU_SETSIZE;
    }
  }
#endif

  // wait for thread/fibre creation
  shim_barrier_wait(cbar);

  // set up alarm
#if !defined __U_CPLUSPLUS__
  timer_t timer;
  struct sigaction sa;
  sa.sa_handler = alarmHandler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  SYSCALL(sigaction(SIGALRM, &sa, 0));
  SYSCALL(timer_create(CLOCK_REALTIME, nullptr, &timer));
  itimerspec tval = { {1,0}, {1,0} };
#endif

  // start experiment
  running = true;
#if defined __U_CPLUSPLUS__
  ticks -= 1;
  AlarmTask at(uDuration(1,0));
#else
  SYSCALL(timer_settime(timer, 0, &tval, nullptr));
#endif

  // signal start
  mytime_t startTime = now();
  shim_barrier_wait(sbar);

  // join threads
  for (unsigned int i = 0; i < fibreCount; i += 1) shim_thread_destroy(workers[i].runner);
  mytime_t endTime = now();

#if defined MORDOR_MAIN
  poolScheduler->stop();
#endif

  // collect and print work results
  unsigned long long wsum = 0;
  double wsum2 = 0;
  for (unsigned int i = 0; i < fibreCount; i += 1) {
    wsum += workers[i].counter;
    wsum2 += pow(workers[i].counter, 2);
  }
  unsigned long long wavg = wsum/fibreCount;
  unsigned long long wstd = (unsigned long long)sqrt(wsum2 / fibreCount - pow(wavg, 2));
  printf("work - total: %llu rate: %llu fairness: %llu/%llu\n", wsum, wsum/duration, wavg, wstd);

  // collect and print lock results
  unsigned long long lsum = 0;
  double lsum2 = 0;
  for (unsigned int i = 0; i < lockCount; i += 1) {
    lsum += locks[i].counter;
    lsum2 += pow(locks[i].counter, 2);
  }
  unsigned long long lavg = lsum/lockCount;
  unsigned long long lstd = (unsigned long long)sqrt(lsum2 / lockCount - pow(lavg, 2));
  printf( "lock - total: %llu rate: %llu fairness: %llu/%llu\n", lsum, lsum/duration, lavg, lstd );

  // print timing information
  if (yieldExperiment) {
    printf("time spent (nanoseconds): %ld\n" , diff_to_ns(endTime, startTime));
    printf("time per yield: %lld\n", diff_to_ns(endTime, startTime) / (wsum / threadCount));
  }

  // exit hard for performance experiments
  EXITMAIN;

  // clean up
  free(workers);
  free(locks);

  // destroy fibre processors
#if defined MORDOR_MAIN
  delete poolScheduler;
#elif defined BOOST_VERSION
  boost_finalize(threadCount);
#elif defined  __U_CPLUSPLUS__
  delete [] proc;
#elif defined QTHREAD_VERSION
  qthread_finalize();
#endif

  // done
  EXITMAIN;

  shim_barrier_destroy(sbar);
  shim_barrier_destroy(cbar);
}

#if defined ARACHNE_H_
int main(int argc, char** argv){
  Arachne::init(&argc, (const char**)argv);
  AppMain(argc, argv);
  Arachne::shutDown();
  Arachne::waitForTermination();
}
#endif
