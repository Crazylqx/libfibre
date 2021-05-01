// **** libfibre options - event handling

//#define TESTING_PROCESSOR_POLLER      1 // poll events directly from each worker
#define TESTING_CLUSTER_POLLER_FIBRE  1 // per-cluster poller: fibre vs. pthread
//#define TESTING_POLLER_FIBRE_SPIN 65536 // poller fibre: spin loop of NB polls
#define TESTING_LAZY_FD_REGISTRATION  1 // lazy vs. eager registration after fd creation
//#define TESTING_ONESHOT_REGISTRATION  1 // use oneshot event polling

/******************************** lock options ********************************/

//#define TESTING_LOCK_RECURSION        1 // enable mutex recursion in C interface

//#define WORKER_LOCK_TYPE BinaryLock<>
//#define WORKER_LOCK_TYPE OsLock<4,1024,1>

/******************************** sanity checks ********************************/

#if !TESTING_LOADBALANCING
  #if TESTING_CLUSTER_POLLER_FIBRE
    #warning disabling TESTING_CLUSTER_POLLER_FIBRE
    #undef TESTING_CLUSTER_POLLER_FIBRE
  #endif
#endif

#if TESTING_ONESHOT_REGISTRATION
  #if TESTING_LAZY_FD_REGISTRATION
    #warning disabling TESTING_LAZY_FD_REGISTRATION
    #undef TESTING_LAZY_FD_REGISTRATION
  #endif
#endif
