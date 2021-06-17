// **** libfibre options - event handling

//#define TESTING_WORKER_POLLER         1 // poll events directly from each worker
#define TESTING_CLUSTER_POLLER_FIBRE  1 // per-cluster poller(s): fibre vs. pthread
#define TESTING_CLUSTER_POLLER_FLOAT  1 // per-cluster poller fibres(s): float vs. background
//#define TESTING_POLLER_FIBRE_SPIN 65536 // poller fibre: spin loop of NB polls
#define TESTING_ONESHOT_REGISTRATION  1 // use oneshot event polling

/******************************** lock options ********************************/

//#define TESTING_LOCK_RECURSION        1 // enable mutex recursion in C interface

//#define WORKER_LOCK_TYPE BinaryLock<>

/******************************** sanity checks ********************************/

#if !TESTING_LOADBALANCING && TESTING_CLUSTER_POLLER_FIBRE
  #error TESTING_CLUSTER_POLLER_FIBRE requires TESTING_LOADBALANCING
#endif
