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
#include <algorithm>
#include <chrono>
#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <cmath>
#include <csignal>
#include <unistd.h> // getopt

using namespace std;

#if __FreeBSD__
#include <sys/cpuset.h>
#include <pthread_np.h>
typedef cpuset_t cpu_set_t;
#endif

#ifndef VARIANT

#define __LIBFIBRE__
#include "libfibre/fibre.h"

#else /* VARIANT */

#ifndef SYSCALL
#include "syscall_macro.h"
#define SYSCALL(call)   SYSCALL_CMP(call,==,0,0)
#define SYSCALLIO(call) SYSCALL_CMP(call,>=,0,0)
#endif /* SYSCALL */

#include VARIANT

#endif /* VARIANT */

// simulate yield-based busy-locking as in qthreads, boost
#define TRY_YIELD_LOCK 0

#if defined MORDOR_MAIN || BOOST_VERSION || QTHREAD_VERSION
#if TRY_YIELD_LOCK
#error TRY_YIELD_LOCK not supported with boost, mordor, or qthread
#endif
#endif

// configuration default settings
static unsigned int threadCount = 2;
static unsigned int duration = 10;
static unsigned int fibreCount = 4;
static unsigned int lockCount = 1;
static unsigned int unlocked = 10000;
static unsigned int work_locked = 10000;

static bool yieldFlag = false;
static bool serialFlag = false;
static bool affinityFlag = false;
static bool calibration = false;

static bool yieldExperiment = false;

// worker descriptor
struct Worker {
  Fibre* runner;
  volatile unsigned long long counter;
};

// lock descriptor
struct Lock {
  FibreMutex mutex;
  volatile unsigned long long counter;
};

// arrays of worker/lock descriptors
static Worker* workers = nullptr;
static Lock* locks = nullptr;

static FibreBarrier* cbar = nullptr;
static FibreBarrier* sbar = nullptr;

// manage experiment duration
static unsigned int ticks = 0;
static volatile bool running = false;

// alarm invoked every second
static void alarmHandler(int) {
  ticks += 1;
  if (ticks >= duration) {
    cerr << '\r' << flush;
    running = false;
  } else {
    cerr << '\r' << ticks << flush;
  }
}

// help message
static void usage(const char* prog) {
  cerr << "usage: " << prog << " -d <duration (secs)> -f <total fibres> -l <locks> -t <system threads> -u <unlocked work> -w <locked work> -s -y -a -c" << endl;
}

// command-line option processing
static bool opts(int argc, char** argv) {
  for (;;) {
    int option = getopt( argc, argv, "d:f:l:t:u:w:syacYh?" );
    if ( option < 0 ) break;
    switch(option) {
    case 'd': duration = atoi(optarg); break;
    case 'f': fibreCount = atoi(optarg); break;
    case 'l': lockCount = atoi(optarg); break;
    case 't': threadCount = atoi(optarg); break;
    case 'u': unlocked = atoi(optarg); break;
    case 'w': work_locked = atoi(optarg); break;
    case 's': serialFlag = true; break;
    case 'y': yieldFlag = true; break;
    case 'a': affinityFlag = true; break;
    case 'c': calibration = true; break;
    case 'Y': yieldExperiment = true; break;
    case 'h':
    case '?':
      usage(argv[0]);
      return false;
    default:
      cerr << "unknown option -" << (char)option << endl;
      usage(argv[0]);
      return false;
    }
  }
  if (argc != optind) {
    cerr << "unknown argument - " << argv[optind] << endl;
    usage(argv[0]);
    return false;
  }
  if (duration == 0 || fibreCount == 0 || lockCount == 0 || threadCount == 0) {
    cerr << "none of -d, -f, -l, -t can be zero" << endl;
    usage(argv[0]);
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

using chrono::high_resolution_clock;

static uint64_t timerOverhead = 0;

static void calibrateTimer() {
  high_resolution_clock::time_point start = high_resolution_clock::now();
  high_resolution_clock::time_point tmp;
  for (unsigned int i = 0; i < (1 << 24) - 1; i += 1) {
    tmp = high_resolution_clock::now();
  }
  (void)tmp;
  high_resolution_clock::time_point end = high_resolution_clock::now();
  chrono::nanoseconds d = end - start;
  timerOverhead = d.count() / (1 << 24);
}

static unsigned int calibrateInterval(unsigned int period) {
  // set up work buffer
  int buffer[workBufferSize];
  for (int i = 0; i < workBufferSize; i += 1) buffer[i] = random() % 1024;

  unsigned int low = 1;
  unsigned int high = 2;
  unsigned int runs = (1<<28) / period;
  cout << period << "ns - upper bound:";
  for (;;) {
    cout << ' ' << high << flush;
    high_resolution_clock::time_point start = high_resolution_clock::now();
    for (unsigned int i = 0; i < runs; i++) dowork(buffer, high);
    high_resolution_clock::time_point end = high_resolution_clock::now();
    chrono::nanoseconds d = end - start;
    if ((d.count() - timerOverhead) / runs > period) break;
    high = high * 2;
  }
  cout << endl;
  cout << "binary search:";
  for (;;) {
    cout << " [" << low << ':' << high << ']' << flush;
    unsigned int next = (low + high) / 2;
    if (next == low) break;
    static const int SampleCount = 3;
    vector<unsigned long> samples(SampleCount);
    for (int s = 0; s < SampleCount; s += 1) {
      high_resolution_clock::time_point start = high_resolution_clock::now();
      for (unsigned int i = 0; i < runs; i++) dowork(buffer, next);
      high_resolution_clock::time_point end = high_resolution_clock::now();
      samples[s] = ((end - start).count() - timerOverhead) / runs;
    }
    sort(samples.begin(), samples.end());
    if (samples[SampleCount/2] > period) high = next;
    else low = next;
  }
  cout << endl;
  assert(low + 1 == high);
  return high;
}

static void yielder(void* arg) {
  // signal creation
  cbar->wait();
  // wait for start signal
  sbar->wait();
  unsigned int num = (uintptr_t)arg;

  unsigned long long count = 0;
  while (running) {
    Fibre::yield();
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
  cbar->wait();
  // wait for start signal
  sbar->wait();
  // run loop
  while (running) {
    // unlocked work
    dowork(buffer, unlocked);
    // locked work and counters
#if TRY_YIELD_LOCK
    while (!locks[lck].mutex.tryAcquire()) Fibre::yield();
#else
    locks[lck].mutex.acquire();
#endif
    dowork(buffer, work_locked);
    workers[num].counter += 1;
    locks[lck].counter += 1;
    locks[lck].mutex.release();
    if (yieldFlag) Fibre::yield();
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
void AppMain(int argc, char** argv) {
#elif defined MORDOR_MAIN
MORDOR_MAIN(int argc, char *argv[]) {
#elif defined __U_CPLUSPLUS__
void uMain::main() {
#else
int main(int argc, char** argv) {
#endif
  // parse command-line arguments
  if (!opts(argc, argv)) EXITMAIN;

  // set up random number generator
  srandom(time(nullptr));

  // run timer calibration, if requested
  if (calibration) {
    calibrateTimer();
    cout << "time overhead: " << timerOverhead << endl;
    unsigned int l = calibrateInterval(work_locked);
    cout << "WORK: -w " << l << endl;
    unsigned int u = calibrateInterval(unlocked);
    cout << "UNLOCKED work: -u " << u << endl;
    cout << endl;
    cout << "WARNING: these numbers are not necessarily very accurate.";
    cout << " Double-check the actual runtime with 'perf'" << endl;
    cout << endl;
    EXITMAIN;
  }

  // create system processors (pthreads)
#if defined MORDOR_MAIN
  poolScheduler = new WorkerPool(threadCount);
#elif defined BOOST_VERSION
  boost_init(threadCount);
#elif defined __LIBFIBRE__ || __U_CPLUSPLUS__
  OsProcessor* proc = new OsProcessor[threadCount - 1];
#elif defined QTHREAD_VERSION
  setenv("QTHREAD_STACK_SIZE", "65536", 1);
  qthread_init(threadCount);
#elif defined _FIBER_FIBER_H_
  fiber_manager_init(threadCount);
#endif

#if 0 // test deadlock behaviour
  FibreBarrier deadlock(2);
  deadlock.wait();
#endif

#if defined __LIBFIBRE__
  if (affinityFlag) {
    cpu_set_t allcpus;
    CPU_ZERO(&allcpus);
    cpu_set_t onecpu;
    CPU_ZERO(&onecpu);
    SYSCALL(pthread_getaffinity_np(pthread_self(), sizeof(allcpus), &allcpus));
    int cpu = 0;
    while (!CPU_ISSET(cpu, &allcpus)) cpu = (cpu + 1) % CPU_SETSIZE;
    CPU_SET(cpu, &onecpu);
    SYSCALL(pthread_setaffinity_np(pthread_self(), sizeof(onecpu), &onecpu));
    CPU_CLR(cpu, &onecpu);
    cpu = (cpu + 1) % CPU_SETSIZE;
    for (unsigned int i = 0; i < threadCount - 1; i += 1) {
      while (!CPU_ISSET(cpu, &allcpus)) cpu = (cpu + 1) % CPU_SETSIZE;
      CPU_SET(cpu, &onecpu);
      SYSCALL(pthread_setaffinity_np(proc[i].getSysID(), sizeof(onecpu), &onecpu));
      CPU_CLR(cpu, &onecpu);
      cpu = (cpu + 1) % CPU_SETSIZE;
    }
  }
#endif

  // create barriers
  cbar = new FibreBarrier(fibreCount + 1);
  sbar = new FibreBarrier(fibreCount + 1);

  // create locks
  locks = new Lock[lockCount];
  for (unsigned int i = 0; i < lockCount; i += 1) {
    locks[i].counter = 0;
  }

  // create threads
  workers = new Worker[fibreCount];
  for (unsigned int i = 0; i < fibreCount; i += 1) {
    workers[i].runner = new Fibre(yieldExperiment ? yielder : worker, (void*)uintptr_t(i));
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
      SYSCALL(workers[i].runner->setaffinity(sizeof(onecpu), &onecpu));
      CPU_CLR(cpu, &onecpu);
      cpu = (cpu + 1) % CPU_SETSIZE;
    }
  }
#endif

  // wait for thread/fibre creation
  cbar->wait();

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
  high_resolution_clock::time_point startTime = high_resolution_clock::now();
  sbar->wait();

  // join threads
  for (unsigned int i = 0; i < fibreCount; i += 1) delete workers[i].runner;
  high_resolution_clock::time_point endTime = high_resolution_clock::now();

#if defined MORDOR_MAIN
  poolScheduler->stop();
#endif

  // print configuration
  cout << "threads: " << threadCount << " workers: " << fibreCount << " locks: " << lockCount;
  if (affinityFlag) cout << " affinity";
  if (serialFlag) cout << " serial";
  if (yieldFlag) cout << " yield";
  cout << endl;
  cout << "duration: " << duration << " locked work: " << work_locked << " unlocked work: " << unlocked << endl;

  // collect and print work results
  unsigned long long wsum = 0;
  double wsum2 = 0;
  for (unsigned int i = 0; i < fibreCount; i += 1) {
    wsum += workers[i].counter;
    wsum2 += pow(workers[i].counter, 2);
  }
  unsigned long long wavg = wsum/fibreCount;
  unsigned long long wstd = (unsigned long long)sqrt(wsum2 / fibreCount - pow(wavg, 2));
  cout << "work - total: " << wsum << " rate: " << wsum/duration << " fairness: " << wavg << '/' << wstd << endl;

  // collect and print lock results
  unsigned long long lsum = 0;
  double lsum2 = 0;
  for (unsigned int i = 0; i < lockCount; i += 1) {
    lsum += locks[i].counter;
    lsum2 += pow(locks[i].counter, 2);
  }
  unsigned long long lavg = lsum/lockCount;
  unsigned long long lstd = (unsigned long long)sqrt(lsum2 / lockCount - pow(lavg, 2));
  cout << "lock - total: " << lsum << " rate: " << lsum/duration << " fairness: " << lavg << '/' << lstd << endl;

  // print timing information
  if (yieldExperiment) {
    cout << "time spent (nanoseconds): " << (endTime - startTime).count() << endl;
    cout << "time per yield: " << (endTime - startTime).count() / (wsum / threadCount)  << endl;
  }

  // exit hard for performance experiments
  EXITMAIN;

  // clean up
  delete [] workers;
  delete [] locks;
  delete sbar;
  delete cbar;

  // destroy fibre processors
#if defined MORDOR_MAIN
  delete poolScheduler;
#elif defined BOOST_VERSION
  boost_finalize(threadCount);
#elif defined  __LIBFIBRE__ || __U_CPLUSPLUS__
  delete [] proc;
#elif defined QTHREAD_VERSION
  qthread_finalize();
#endif

  // done
  EXITMAIN;
}

#if defined ARACHNE_H_
int main(int argc, char** argv){
  Arachne::init(&argc, (const char**)argv);
  AppMain(argc, argv);
  Arachne::shutDown();
  Arachne::waitForTermination();
}
#endif
