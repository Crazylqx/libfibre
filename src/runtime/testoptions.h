// **** general options - testing

#define TESTING_ENABLE_ASSERTIONS     1
#define TESTING_ENABLE_STATISTICS     1
#define TESTING_ENABLE_DEBUGGING      1

// **** general options - alternative design

#define TESTING_LOADBALANCING         1 // enable load balancing using ISRS
//#define TESTING_STICKY_STEALING       1 // enable sticky work-stealing
//#define TESTING_SHARED_READYQUEUE     1 // use shared ready queue, instead of stealing
//#define TESTING_LOCKED_READYQUEUE     1 // locked vs. lock-free ready queue
#define TESTING_NEMESIS_READYQUEUE    1 // lock-free: nemesis vs. stub-based MPSC
//#define TESTING_PLACEMENT_STAGING     1 // load-based staging vs. round-robin placement
//#define TESTING_IDLE_SPIN         65536 // spin before entering idle loop
//#define TESTING_HALT_SPIN         65536 // spin before halting worker thread/core
//#define TESTING_MUTEX_FIFO            1 // use baton-passing fifo mutex
//#define TESTING_MUTEX_BARGING         1 // use blocking/barging mutex
//#define TESTING_MUTEX_SPIN            1 // spin before block in non-fifo mutex

#include "runtime-glue/testoptions.h"

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
