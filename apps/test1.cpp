#include "fibre.h"

#include <iostream>
#include <sys/wait.h> // waitpid

using namespace std;

static volatile size_t counter = 0;

static FibreMutex testmtx;

static fibre_once_t once_test = PTHREAD_ONCE_INIT;
static fibre_key_t key_test;

static void key_finish(void* value) {
  cout << "finish " << (char)(uintptr_t)value << endl;
}

static void once_init() {
  cout << "once init" << endl;
  SYSCALL(fibre_key_create(&key_test, key_finish));
}

static void f1main() {
  fibre_once(&once_test, once_init);
  SYSCALL(fibre_setspecific(key_test, (void*)'A'));
  cout << "F1 1" << endl;
  Fibre::yield();
  cout << "F1 2" << endl;
  cout << "F1 3" << endl;
  for (size_t i = 0; i < 100000; i += 1) {
    testmtx.acquire();
    counter += 1;
    testmtx.release();
  }
  cout << "F1 specific " << (char)(uintptr_t)fibre_getspecific(key_test) << endl;
}

static void f2main() {
  fibre_once(&once_test, once_init);
  SYSCALL(fibre_setspecific(key_test, (void*)'B'));
  cout << "F2 1" << endl;
  Fibre::yield();
  cout << "F2 2" << endl;
  cout << "F2 3" << endl;
  for (size_t i = 0; i < 100000; i += 1) {
    testmtx.acquire();
    counter += 1;
    testmtx.release();
  }
  cout << "F2 specific " << (char)(uintptr_t)fibre_getspecific(key_test) << endl;
}

static FibreSemaphore tmx(0);

static void f3main() {
  fibre_once(&once_test, once_init);
  SYSCALL(fibre_setspecific(key_test, (void*)'C'));
  Time ct;
  SYSCALL(clock_gettime(CLOCK_REALTIME, &ct));
  cout << ct.tv_sec << '.' << ct.tv_nsec << endl;
  Time to = ct + Time(1,0);
  if (!tmx.P(to)) {
    cout << "timeout" << endl;
  }
  SYSCALL(clock_gettime(CLOCK_REALTIME, &ct));
  cout << ct.tv_sec << '.' << ct.tv_nsec << endl;
  cout << "F3 specific " << (char)(uintptr_t)fibre_getspecific(key_test) << endl;
}

int main(int argc, char** argv) {
  FibreInit();
  pid_t p = SYSCALL(FibreFork());
  cout << "Hello world " << getpid() << endl;
  if (p) {
    SYSCALL(waitpid(p, nullptr, 0));
    cout << "Child " << p << " finished" << endl;
  }
  Time ct;
  SYSCALL(clock_gettime(CLOCK_REALTIME, &ct));
  cout << ct.tv_sec << '.' << ct.tv_nsec << endl;
  if (argc > 1) Fibre::usleep(Time::USEC * atoi(argv[1]));
  else Fibre::usleep(1000);
  SYSCALL(clock_gettime(CLOCK_REALTIME, &ct));
  cout << ct.tv_sec << '.' << ct.tv_nsec << endl;
  Context::CurrCluster().addWorkers(1);
  Fibre* f1 = (new Fibre)->run(f1main);
  Fibre* f2 = (new Fibre)->run(f2main);
  Fibre* f3 = (new Fibre)->run(f3main);
  cout << "M 1" << endl;
  Fibre::yield();
  cout << "M 2" << endl;
  f1->join();
  delete f1;
  cout << "f1 gone" << endl;
  delete f2;
  cout << "f2 gone" << endl;
  delete f3;
  cout << "f3 gone" << endl;
  cout << counter << endl;
  SYSCALL(fibre_key_delete(key_test));
  return 0;
}
