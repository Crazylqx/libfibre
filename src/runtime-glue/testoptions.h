// **** libfibre options - event handling

//#define TESTING_PROCESSOR_POLLER      1 // poll events directly from each worker
#define TESTING_CLUSTER_POLLER_FIBRE  1 // per-cluster poller: fibre vs. pthread
#define TESTING_POLLER_FIBRE_SPIN 65536 // poller fibre: spin loop of NB polls
#define TESTING_LAZY_FD_REGISTRATION  1 // lazy vs. eager registration after fd creation
//#define TESTING_IOYIELD_CONDITIONAL   1 // yield-before-read only when FD not registered for poll
//#define TESTING_TRY_IO_BEFORE_YIELD   1 // make one non-blocking I/O attempt before yield

/******************************** sanity checks ********************************/

#if !TESTING_LOADBALANCING
  #if TESTING_CLUSTER_POLLER_FIBRE
    #warning disabling TESTING_CLUSTER_POLLER_FIBRE
    #undef TESTING_CLUSTER_POLLER_FIBRE
  #endif
#endif
