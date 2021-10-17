// **** general options - testing

#define TESTING_ENABLE_ASSERTIONS     1
#define TESTING_ENABLE_STATISTICS     1
#define TESTING_ENABLE_DEBUGGING      1

// **** general options - alternative design

#define TESTING_LOADBALANCING         1 // enable load balancing using ISRS
#define TESTING_IDLE_MANAGER          1 // enable idle manager; suspend workers
#define TESTING_DEFAULT_AFFINITY      1 // enable default affinity (no sticky stealing)
//#define TESTING_WAKE_FRED_WORKER      1 // idle manager: wake fred's worker vs any worker
//#define TESTING_PLACEMENT_STAGING     1 // load-based staging vs. round-robin placement
//#define TESTING_SHARED_READYQUEUE     1 // use shared ready queue, instead of stealing
//#define TESTING_LOCKED_READYQUEUE     1 // locked vs. lock-free ready queue
//#define TESTING_STUB_QUEUE            1 // nemesis vs. stub-based MPSC lock-free queue
//#define TESTING_IDLE_SPIN         65536 // spin before entering idle loop
//#define TESTING_HALT_SPIN         65536 // spin before halting worker thread/core

#include "runtime-glue/testoptions.h"

/******************************** lock options ********************************/

//#define FAST_MUTEX_TYPE SimpleMutex0<false>

//#define FRED_MUTEX_TYPE FastMutex

/******************************** sanity checks ********************************/

#if !TESTING_LOADBALANCING && TESTING_SHARED_READYQUEUE
  #error TESTING_SHARED_READYQUEUE requires TESTING_LOADBALANCING
#endif

#if !TESTING_LOADBALANCING && TESTING_PLACEMENT_STAGING
  #error TESTING_PLACEMENT_STAGING requires TESTING_LOADBALANCING
#endif
