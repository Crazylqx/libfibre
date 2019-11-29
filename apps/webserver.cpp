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
#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <list>
#include <map>
#include <cassert>
#include <csignal>
#include <cstring>
#include <unistd.h>      // getopt, close
#include <sys/types.h>   // various system types
#include <sys/socket.h>  // sockets
#include <arpa/inet.h>   // htons
#include <netinet/in.h>  // sockaddr_in
#include <netinet/tcp.h> // SOL_TCP options

using namespace std;

#if __FreeBSD__
#include <sys/cpuset.h>
#include <pthread_np.h>
typedef cpuset_t cpu_set_t;
#endif

#include "Garage.h"

#ifndef VARIANT

#define __LIBFIBRE__
#include "libfibre/fibre.h"
#define CurrProcessor Context::CurrProcessor
#define CurrCluster Context::CurrCluster
#else /* VARIANT */

#ifndef SYSCALL
#include "syscall_macro.h"
#define SYSCALL(call)   SYSCALL_CMP(call,==,0,0)
#define SYSCALLIO(call) SYSCALL_CMP(call,>=,0,0)
#define TRY_SYSCALL(call,code)   SYSCALL_CMP(call,==,0,code)
#endif /* SYSCALL */

#include VARIANT

#if defined __U_CPLUSPLUS__
#include "uSocket.h"
#else
#define lfSocket socket
#define lfBind   ::bind
#define lfListen listen
#define lfAccept accept
#define lfClose  close
template<typename T, class... Args>
T lfInput( T (*readfunc)(int, Args...), int fd, Args... a) {
  return readfunc(fd, a...);
}
template<typename T, class... Args>
T lfOutput( T (*writefunc)(int, Args...), int fd, Args... a) {
  return writefunc(fd, a...);
}
#endif /* __U_CPLUSPLUS__ */

#endif /* VARIANT */

#include "picohttpparser/picohttpparser.h"
#include "picohttpparser/picohttpparser.c"

// configuration default settings
static unsigned int clusterSize = 64;
static unsigned int scopeCount = 1;
static unsigned int listenerCount = 1; // 0 -> listener per connection
static unsigned int threadCount = 1;
static bool affinityFlag = false;
static bool groupAffinityFlag = false;
static bool singleServerSocket = true;

// system configuration, if needed (set listen backlog to maximum value)
static int maxBacklog = -1;

// define request handler
typedef void (*UrlHandler)(void* fd, const char* path, int minor_version);

// define routing table
static map<const string,UrlHandler> routingTable;

typedef Garage<FibreMutex,FibreCondition> FibreGarage;

static FibreGarage& CurrGarage() {
#if defined __LIBFIBRE__
  return *reinterpret_cast<FibreGarage*>(Context::CurrEventScope().getClientData());
#else
  static FibreGarage garage;
  return garage;
#endif
}

// help message
static void usage(const char* prog) {
  cerr << "usage: " << prog << " -c <cluster size> -e <event scope count> -l <listener count> -t <system threads> -a -g -m -r" << endl;
}

// fibre counting
static volatile size_t connections = 0;
static volatile size_t connectionFibres = 0;

static void exitHandler(int sig) {
  if (sig == SIGINT) cout << endl;
  cout << "connections: " << connections << endl;
  cout << "fibres: " << connectionFibres << endl;
  exit(0);
}

// command-line option processing
static void opts(int argc, char** argv) {
  for (;;) {
    int option = getopt( argc, argv, "c:e:l:t:agmh?" );
    if ( option < 0 ) break;
    switch(option) {
    case 'c': clusterSize = atoi(optarg); break;
    case 'e': scopeCount = atoi(optarg); break;
    case 'l': listenerCount = atoi(optarg); break;
    case 't': threadCount = atoi(optarg); break;
    case 'a': affinityFlag = true; break;
    case 'g': groupAffinityFlag = true; break;
    case 'm': singleServerSocket = false; break;
    case 'h':
    case '?':
      usage(argv[0]);
      exit(0);
    default:
      cerr << "unknown option -" << (char)option << endl;
      usage(argv[0]);
      exit(1);
    }
  }
  if (argc != optind) {
    cerr << "unknown argument - " << argv[optind] << endl;
    usage(argv[0]);
    exit(1);
  }
  if (clusterSize == 0 || threadCount == 0) {
    cerr << "none of -c, -t can be zero" << endl;
  }
#if defined __U_CPLUSPLUS__
  singleServerSocket = true;
#endif
}

static const char* RESPONSE = "HTTP/1.1 200 OK\r\n" \
                              "Content-Length: 15\r\n" \
                              "Content-Type: text/html\r\n" \
                              "Connection: keep-alive\r\n" \
                              "Server: testserver\r\n" \
                              "\r\n" \
                              "Hello, World!\r\n";

static const size_t RLEN = strlen(RESPONSE);

static inline void sendResponse(void* connFD, int minor_version, const char* hdr, size_t clen, const char* content) {
#if defined __U_CPLUSPLUS__
  try {
    ((uSocketAccept*)connFD)->send((char*)RESPONSE, RLEN, (int)MSG_NOSIGNAL);
  } catch(uSocketAccept::WriteFailure& rderr) {}
#else
  lfOutput(send, (uintptr_t)connFD, (const void*)RESPONSE, RLEN, (int)MSG_NOSIGNAL);
#endif
}

static void HelloWorld(void* connFD, const char* path, int minor_version) {
  sendResponse(connFD, minor_version, " 200 OK", 15, "Hello, World!\r\n");
}

// derived from example code at https://github.com/h2o/picohttpparser
static inline bool connHandler(void* connFD) {
  char buf[4096];
  size_t buflen = 0, plen = 0, prevbuflen = 0;
  size_t method_len, path_len;
  const char *method, *path;
  size_t num_headers;
  struct phr_header headers[16];
  int minor_version;

  for (;;) {
    /* read request(s) */
    ssize_t rret;
#if defined __U_CPLUSPLUS__
    try {
      rret = ((uSocketAccept*)connFD)->recv(buf + buflen, sizeof(buf) - buflen, 0);
    } catch(uSocketAccept::ReadFailure& rderr) {
      goto closeAndOut;
    }
#else
    while ((rret = lfInput(recv, (uintptr_t)connFD, (void*)(buf + buflen), sizeof(buf) - buflen, 0)) < 0 && _SysErrno() == EINTR);
#endif
    if (rret == 0) {
      if (buflen == plen) {
//        cerr << "connection closed: FD " << (uintptr_t)connFD << endl;
      } else {
        cerr << "unfinished partial request: FD " << (uintptr_t)connFD << endl;
      }
      goto closeAndOut;
    } else if (rret < 0) {
      if (_SysErrno() == ECONNRESET) {
//        cerr << "ECONNRESET: FD " << (uintptr_t)connFD << endl;
      } else {
        cerr << "read error: FD " << (uintptr_t)connFD << ' ' << _SysErrno() << endl;
      }
      goto closeAndOut;
    }
    buflen += rret;

    for (;;) {
      /* parse request(s) */
      num_headers = sizeof(headers) / sizeof(headers[0]);
      ssize_t pret = phr_parse_request(buf + plen, buflen - plen, &method, &method_len, &path, &path_len, &minor_version, headers, &num_headers, prevbuflen);
      if (pret > 0) {
#if 0
        printf("request is %li bytes long\n", pret);
        printf("method is %.*s\n", (int)method_len, method);
        printf("path is %.*s\n", (int)path_len, path);
        printf("HTTP version is 1.%d\n", minor_version);
        printf("headers:\n");
        for (size_t i = 0; i != num_headers; ++i) {
          printf("%.*s: %.*s\n", (int)headers[i].name_len, headers[i].name, (int)headers[i].value_len, headers[i].value);
        }
#endif
        *(char*)(method + method_len) = 0;
        *(char*)(path + path_len) = 0;
        if (!strcmp(method, "GET")) {
         auto it = routingTable.find(path);
          if (it == routingTable.end()) {
            sendResponse(connFD, minor_version, " 404 Not Found", 0, nullptr);
          } else {
            it->second(connFD, path, minor_version);
          }
        } else {
          sendResponse(connFD, minor_version, " 405 Method Not Allowed", 0, nullptr);
        }
        if (!minor_version) goto closeAndOut;
        for (size_t i = 0; i != num_headers; ++i) {
          if ( !strncasecmp(headers[i].name, "connection", headers[i].name_len)
            && !strncasecmp(headers[i].value, "close", headers[i].value_len)) goto closeAndOut;
        }
        plen += pret;
        if (plen == buflen) return true;
        prevbuflen = 0;
      } else if (pret == -1) {
        cerr << "parse error - FD " << (uintptr_t)connFD << endl;
        goto closeAndOut;
      } else { assert(pret == -2);
        /* request is incomplete, need to read more data */
        if (buflen == sizeof(buf)) {
          cerr << "buffer overflow - FD " << (uintptr_t)connFD << endl;
          goto closeAndOut;
        }
        prevbuflen = buflen - plen;
        break;
      }
    }
  }

closeAndOut:
#if defined __U_CPLUSPLUS__
  delete (uSocketAccept*)connFD;
#else
  TRY_SYSCALL(lfClose((uintptr_t)connFD),ECONNRESET);
#endif
  return false;
}

#if defined __U_CPLUSPLUS__
static uSocketServer* create_socket() {
  return new uSocketServer(8800, SOCK_STREAM, 0, maxBacklog);
}

#else

static int create_socket(bool singleAccept = false) {
  int fd = SYSCALLIO(lfSocket(AF_INET, SOCK_STREAM, 0));
  const struct linger l = { 1, 0 };
  SYSCALL(setsockopt(fd, SOL_SOCKET, SO_LINGER, (const void*)&l, sizeof(l)));
  int on = 1;
  SYSCALL(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const void*)&on, sizeof(on)));
  SYSCALL(setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, (const void*)&on, sizeof(on)));
#if __FreeBSD__
  sockaddr_in addr = { sizeof(sockaddr_in), AF_INET, htons(8800), { INADDR_ANY }, { 0 } };
#else
  int qlen = 5;
  SYSCALL(setsockopt(fd, IPPROTO_TCP, TCP_FASTOPEN, (const void*)&qlen, sizeof(qlen)));
  SYSCALL(setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const void*)&on, sizeof(on)));
  SYSCALL(setsockopt(fd, IPPROTO_TCP, TCP_DEFER_ACCEPT, (const void*)&on, sizeof(on)));
  sockaddr_in addr = { AF_INET, htons(8800), { INADDR_ANY }, { 0 } };
#endif
  SYSCALL(lfBind(fd, (sockaddr*)&addr, sizeof(addr)));
  if (singleAccept) SYSCALL(lfListen(fd, 0));
  else SYSCALL(lfListen(fd, maxBacklog));
#if __FreeBSD__
  struct accept_filter_arg afa; // see 'man 9 accf_data - set after 'listen'
  bzero(&afa, sizeof(afa));
  strcpy(afa.af_name, "dataready");
  SYSCALL(setsockopt(fd, SOL_SOCKET, SO_ACCEPTFILTER, &afa,	sizeof(afa)));
#endif
  return fd;
}
#endif

static void handler_loop(void* arg) {
  for (;;) {
    __atomic_add_fetch(&connections, 1, __ATOMIC_RELAXED);
    while (connHandler(arg));
    arg = CurrGarage().park();
  }
}

static void acceptor(void* arg) {
#if defined __U_CPLUSPLUS__
  uSocketServer* servFD = arg ? (uSocketServer*)arg : create_socket();
#else
  int servFD = ((intptr_t)arg < 0) ? create_socket() : (intptr_t)arg;
#endif
  for (;;) {
#if defined __U_CPLUSPLUS__
    uSocketAccept* connFD = new uSocketAccept(*servFD);
#else
    uintptr_t connFD = lfAccept(servFD, nullptr, nullptr);
#if __FreeBSD__
    int on = 1;
    SYSCALL(setsockopt(connFD, IPPROTO_TCP, TCP_NODELAY, (const void*)&on, sizeof(on)));
#endif
#endif
    if (!CurrGarage().run((void*)connFD)) {
      __atomic_add_fetch(&connectionFibres, 1, __ATOMIC_RELAXED);
      new Fibre(handler_loop, (void*)connFD);
    }
  }
#if defined __U_CPLUSPLUS__
  if (!arg) delete servFD;
#else
  if ((intptr_t)arg < 0) SYSCALL(lfClose(servFD));
#endif
}

static void acceptor_loop(void* arg) {
#if defined __U_CPLUSPLUS__
  uSocketServer* servFD = arg ? (uSocketServer*)arg : create_socket();
#else
  int servFD = ((intptr_t)arg < 0) ? create_socket(true) : (intptr_t)arg;
#endif
  for (;;) {
#if defined __U_CPLUSPLUS__
    uSocketAccept* connFD = new uSocketAccept(*servFD);
#else
    uintptr_t connFD = lfAccept(servFD, nullptr, nullptr);
#if __FreeBSD__
    int on = 1;
    SYSCALL(setsockopt(connFD, IPPROTO_TCP, TCP_NODELAY, (const void*)&on, sizeof(on)));
#endif
#endif
    if (!CurrGarage().run((void*)arg)) {
      __atomic_add_fetch(&connectionFibres, 1, __ATOMIC_RELAXED);
      new Fibre(acceptor_loop, (void*)arg);
    }
    while (connHandler((void*)connFD));
    CurrGarage().park();
  }
#if defined __U_CPLUSPLUS__
  if (!arg) delete servFD;
#else
  if ((intptr_t)arg < 0) SYSCALL(lfClose(servFD));
#endif
}

static void scopemain(void* arg) {
#if defined __LIBFIBRE__
  FibreGarage garage;
  Context::CurrEventScope().setClientData(&garage);
#endif

#if defined __LIBFIBRE__ || defined __U_CPLUSPLUS__

  // set additional clusters and processors
  unsigned int clusterCount = (threadCount - 1) / clusterSize + 1;
  Cluster** cluster = new Cluster*[clusterCount];
  cluster[0] = &CurrCluster();
  for (unsigned int c = 1; c < clusterCount; c += 1) {
    cluster[c] = new Cluster;
  }
  OsProcessor** sproc = new OsProcessor*[threadCount];
  sproc[0] = &reinterpret_cast<OsProcessor&>(CurrProcessor());
  for (unsigned int t = 1; t < threadCount; t += 1) {
    sproc[t] = new OsProcessor(*cluster[t/clusterSize]);
  }

  if (affinityFlag || groupAffinityFlag) {

    // set processor per-core affinity
#if defined __LIBFIBRE__
    cpu_set_t clustercpus;
    CPU_ZERO(&clustercpus);
    unsigned int cidx = 0;
#endif
    cpu_set_t allcpus;
    CPU_ZERO(&allcpus);
    SYSCALL(pthread_getaffinity_np(pthread_self(), sizeof(allcpus), &allcpus));
    int cpu = 0;
    while (!CPU_ISSET(cpu, &allcpus)) cpu = (cpu + 1) % CPU_SETSIZE;
    cpu += (uintptr_t)arg;
    cpu_set_t onecpu;
    CPU_ZERO(&onecpu);
    for (unsigned int t = 0; t < threadCount; t += 1) {
      while (!CPU_ISSET(cpu, &allcpus)) cpu = (cpu + 1) % CPU_SETSIZE;
      if (affinityFlag) {
        CPU_SET(cpu, &onecpu);
        cout << "thread "<< t << " affinity " << cpu << endl;
#if defined __LIBFIBRE__
        SYSCALL(pthread_setaffinity_np(sproc[t]->getSysID(), sizeof(onecpu), &onecpu));
#else
        sproc[t]->setAffinity(onecpu);
#endif
        CPU_CLR(cpu, &onecpu);
      }
#if defined __LIBFIBRE__
      // set affinity to group of cores
      CPU_SET(cpu, &clustercpus);
      if (((t % clusterSize) == clusterSize-1) || (t == threadCount-1)) { // end of cluster or end of threads
        cout << "cluster " << cidx << " affinity: ";
        for (int j = 0; j < CPU_SETSIZE; j++) if (CPU_ISSET(j, &clustercpus)) cout << ' ' << j;
        cout << endl;
        if (affinityFlag) {
#if !TESTING_CLUSTER_POLLER_FIBRE
          for (size_t pp = 0; pp < cluster[cidx]->getPollerCount(); pp += 1) {
            SYSCALL(pthread_setaffinity_np(cluster[cidx]->getPoller(pp).getSysID(), sizeof(clustercpus), &clustercpus));
          }
#endif
        } else if (groupAffinityFlag) {
          cout << "threads:";
          for (unsigned int x = t - (clusterSize-1); x <= t; x += 1) {
            cout << ' ' << x;
            SYSCALL(pthread_setaffinity_np(sproc[t]->getSysID(), sizeof(clustercpus), &clustercpus));
          }
          cout << endl;
        }
        CPU_ZERO(&clustercpus);
        cidx += 1;
      }
#endif

      cpu += 1;
    } // loop through CPUs
  } // affinityFlag || groupAffinityFlag

#else

  unsigned int clusterCount = 1;

#endif /* __LIBFIBRE__ || __U_CPLUSPLUS__ */

  // create server socket, if needed
#if defined __U_CPLUSPLUS__
  uSocketServer* servFD = singleServerSocket ? new uSocketServer(8800, SOCK_STREAM, 0, 65535) : nullptr;
#else
  uintptr_t servFD = singleServerSocket ? create_socket() :  -1;
#endif

  // create initial listeners
  list<Fibre*> fibreList;
  for (unsigned int c = 0; c < clusterCount; c += 1) {
    if (listenerCount) {
      for (unsigned int i = 0; i < listenerCount; i += 1) {
        Fibre* f = new Fibre(acceptor, (void*)servFD);
        fibreList.push_back(f);
      }
    } else {
      Fibre* f = new Fibre(acceptor_loop, (void*)servFD);
      fibreList.push_back(f);
    }
#if defined __LIBFIBRE__
    Fibre::migrateNow(*cluster[(c+1)%clusterCount]);
#elif defined __U_CPLUSPLUS__
    uThisTask().migrate(*cluster[(c+1)%clusterCount]);
#endif
  }

  // wait for all listeners
  for (Fibre* f : fibreList) delete f;

  // close server socket, if neccessary
  if (singleServerSocket) {
#if defined __U_CPLUSPLUS__
    delete servFD;
#else
    SYSCALL(lfClose(servFD));
#endif
  }

  // clean up
#if defined __U_CPLUSPLUS__
  for (unsigned int c = 1; c < clusterCount; c += 1) delete cluster[c];
  delete [] cluster;
  for (unsigned int t = 1; t < threadCount; t += 1) delete sproc[t];
  delete [] sproc;
#endif
}

int main(int argc, char** argv) {
  // parse command-line arguments
  opts(argc, argv);

  cout << "threads: " << threadCount << " cluster size: " << clusterSize << " listeners: " << listenerCount << " event scopes: " << scopeCount;
  if (affinityFlag) cout << " affinity";
  else if (groupAffinityFlag) cout << " group affinity";
  cout << endl;

#if defined _FIBER_FIBER_H_
  fiber_manager_init(threadCount);
  fiber_io_init();
#endif

  // install exit handler
  struct sigaction sa;
  sa.sa_handler = exitHandler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  SYSCALL(sigaction(SIGHUP, &sa, 0));
  SYSCALL(sigaction(SIGINT, &sa, 0));
  SYSCALL(sigaction(SIGQUIT, &sa, 0));
  SYSCALL(sigaction(SIGTERM, &sa, 0));

  // add routing entry
  routingTable.emplace("/plaintext", HelloWorld);

#if __linux__
  // read max backlog setting
  ifstream f("/proc/sys/net/ipv4/tcp_max_syn_backlog");
  f >> maxBacklog;
#endif

#if defined  __LIBFIBRE__
  EventScope* es = new EventScope[scopeCount-1];
  Fibre** esf = new Fibre*[scopeCount-1];
  for (unsigned int i = 0; i < scopeCount-1; i++) {
    esf[i] = (new Fibre(es[i].getMainCluster()))->run(scopemain, (void*)uintptr_t(threadCount * (i+1)));
  }
#endif

  scopemain((void*)0);

#if defined  __LIBFIBRE__
  for (unsigned int i = 0; i < scopeCount-1; i++) {
    delete esf[i];
  }
#endif

  exitHandler(0);

  // done
#if !defined __U_CPLUSPLUS__
  return 0;
#endif
}
