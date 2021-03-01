// **** general options - testing

#define TESTING_ENABLE_ASSERTIONS     1
#define TESTING_ENABLE_STATISTICS     1
#define TESTING_ENABLE_DEBUGGING      1

// **** general options - alternative design

#define TESTING_LOADBALANCING         1 // enable load balancing using ISRS
//#define TESTING_STICKY_STEALING       1 // enable sticky work-stealing
//#define TESTING_SHARED_READYQUEUE     1 // use shared ready queue, instead of stealing
//#define TESTING_LOCKED_READYQUEUE     1 // locked vs. lock-free ready queue
//#define TESTING_STUB_QUEUE            1 // nemesis vs. stub-based MPSC lock-free queue
//#define TESTING_PLACEMENT_STAGING     1 // load-based staging vs. round-robin placement
//#define TESTING_IDLE_SPIN         65536 // spin before entering idle loop
//#define TESTING_HALT_SPIN         65536 // spin before halting worker thread/core

#include "runtime-glue/testoptions.h"

/******************************** lock options ********************************/

//#define FAST_MUTEX_TYPE SpinMutex<FredBenaphore<LimitedSemaphore0<BinaryLock<>>,true>, 4, 1024, 16>
//#define FAST_MUTEX_TYPE SimpleMutex0<false>
//#define FAST_MUTEX_TYPE SimpleMutex0<true>
//#define FAST_MUTEX_TYPE SpinMutex<MCSTimeoutSemaphore<MCSLock>, 4, 1024, 16> // spinning
#define FAST_MUTEX_TYPE SpinMutex<MCSTimeoutSemaphore<MCSLock>, 0, 0, 0>

//#define FRED_MUTEX_TYPE LockedMutex<WorkerLock, true>  // locked, fifo
//#define FRED_MUTEX_TYPE LockedMutex<WorkerLock, false> // locked, barging
//#define FRED_MUTEX_TYPE SpinMutex<LockedSemaphore<WorkerLock, true>, 4, 1024, 16> // spinning
//#define FRED_MUTEX_TYPE FastMutex
//#define FRED_MUTEX_TYPE SpinMutex<MCSTimeoutSemaphore<MCSLock>, 4, 1024, 16> // spinning
#define FRED_MUTEX_TYPE SpinMutex<MCSTimeoutSemaphore<MCSLock>, 0, 0, 0>

/******************************** sanity checks ********************************/

#if TESTING_SHARED_READYQUEUE
  #if !TESTING_LOADBALANCING
    #warning enabling TESTING_LOADBALANCING for TESTING_SHARED_READYQUEUE
    #define TESTING_LOADBALANCING 1
  #endif
#endif

#if !TESTING_LOADBALANCING
  #if TESTING_PLACEMENT_STAGING
    #warning disabling TESTING_PLACEMENT_STAGING
    #undef TESTING_PLACEMENT_STAGING
  #endif
#endif
