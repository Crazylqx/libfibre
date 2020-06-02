#include "fibre.h"

#include <iostream>

using namespace std;

static volatile size_t counter = 0;

static FibreMutex testmtx;

static void f1main() {
  cout << "F1 1" << endl;
  Fibre::yield();
  cout << "F1 2" << endl;
  cout << "F1 3" << endl;
  for (size_t i = 0; i < 100000; i += 1) {
    testmtx.acquire();
    counter += 1;
    testmtx.release();
  }
}

static void f2main() {
  cout << "F2 1" << endl;
  Fibre::yield();
  cout << "F2 2" << endl;
  cout << "F2 3" << endl;
  for (size_t i = 0; i < 100000; i += 1) {
    testmtx.acquire();
    counter += 1;
    testmtx.release();
  }
}

static FibreSemaphore tmx(0);

static void f3main() {
  Time ct;
  SYSCALL(clock_gettime(CLOCK_REALTIME, &ct));
  cout << ct.tv_sec << '.' << ct.tv_nsec << endl;
  Time to = ct + Time(1,0);
  if (!tmx.P(to)) {
    cout << "timeout" << endl;
  }
  SYSCALL(clock_gettime(CLOCK_REALTIME, &ct));
  cout << ct.tv_sec << '.' << ct.tv_nsec << endl;
}

int main(int argc, char** argv) {
  cout << "Hello world" << endl;
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
  return 0;
}
