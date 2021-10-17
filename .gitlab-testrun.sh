#!/usr/bin/env bash

function init() {
	mkdir -p out
	[ -f /usr/local/lib/liburing.so ] && LOCALURING=/usr/local/lib
	if [ "$(uname -s)" = "FreeBSD" ]; then
		MAKE=gmake
		count=$(expr $(sysctl kern.smp.cpus|cut -c16- || echo 1) / 4)
		[ $count -gt 0 ] || count=1
		$HT && clcnt=$(expr $count \* 2) || clcnt=$(expr $count \* 3)
		clbot=0
		cltop=$(expr $clcnt - 1)
		svtop=$(expr $count \* 4 - 1)
		svlist=$clcnt
		$HT && inc=2 || inc=1
		for ((c=$svlist+$inc;c<=$svtop;c+=$inc)); do
			svlist=$svlist,$c
		done
		$quiet || echo cores - client: $clbot-$cltop server: $svlist
		TASKSET_SERVER="cpuset -l $svlist"
		TASKSET_CLIENT="cpuset -l $clbot-$cltop"
		TEST_MEMCACHED_PORT="sockstat -46l -p 11211"
	else
		MAKE=make
		count=$(expr $(nproc) / 4)
		[ $count -gt 0 ] || count=1
		clbot1=0
		cltop1=$(expr $count - 1)
		clbot2=$(expr $count \* 2)
		cltop2=$(expr $count \* 3 - 1)
		svbot=$(expr $count \* 3)
		svtop=$(expr $count \* 4 - 1)
		$HT && clcnt=$(expr $count \* 2) || clcnt=$(expr $count \* 3)
		$HT && cllst="$clbot1-$cltop1,$clbot2-$cltop2" || cllst="$clbot1-$cltop2"
		$quiet || echo cores - client: $cllst server: $svbot-$svtop
		TASKSET_SERVER="taskset -c $svbot-$svtop"
		TASKSET_CLIENT="taskset -c $cllst"
		TEST_MEMCACHED_PORT="lsof -i :11211"
	fi
}


function error() {
  echo " ERROR " $1
  exit 1
}

function pre() {
	cp src/runtime/testoptions.h src/runtime/testoptions.h.orig
	cp src/runtime-glue/testoptions.h src/runtime-glue/testoptions.h.orig
}

function compile() {
	echo -n "CLEAN"
  ($MAKE clean > out/clean.$2.out; $MAKE -C memcached-1.6.9 clean distclean; $MAKE -C vanilla clean distclean) > out/clean.$2.out 2>> out/clean.$2.err
  echo -n " COMPILE"
  case "$1" in
  stacktest)
		$MAKE DYNSTACK=1 all > out/make.$2.out || error
		;;
  memcached)
	  $MAKE all > out/make.$2.out || error
		FPATH=$PWD
		(cd memcached; ./configure2.sh $FPATH $LOCALURING; cd -) >> out/make.$2.out || error
		$MAKE -C memcached -j $count all >> out/make.$2.out || error
		;;
	vanilla)
		(cd vanilla; ./configure; cd -) > out/make.$2.out || error
		$MAKE -C vanilla -j $count all >> out/make.$2.out || error
		;;
	skip)
		;;
	*)
	  $MAKE all > out/make.$2.out || error
	  ;;

	esac
	echo " DONE"
}

function post() {
	killall -9 threadtest > /dev/null 2> /dev/null
	killall -9 stacktest > /dev/null 2> /dev/null
	killall -9 memcached > /dev/null 2> /dev/null
	$MAKE -C memcached clean distclean > /dev/null 2> /dev/null
	$MAKE -C vanilla clean distclean > /dev/null 2> /dev/null
	$MAKE clean > /dev/null 2> /dev/null
	mv src/runtime/testoptions.h.orig src/runtime/testoptions.h > /dev/null 2> /dev/null
	mv src/runtime-glue/testoptions.h.orig src/runtime-glue/testoptions.h > /dev/null 2> /dev/null
}

function run_skip() {
	echo "test skipped"
}

function run_threadtest() {
	c=$(expr $count \* 2)
	FibrePrintStats=1 timeout 30 ./apps/threadtest -t $c -f $(expr $c \* $c) || exit 1
	FibrePrintStats=1 timeout 30 ./apps/threadtest -t $c -f $(expr $c \* $c) -o 10000 -r -L T || exit 1
}

function run_stacktest() {
	echo -n "STACKTEST: "
	FibrePrintStats=1 timeout 30 apps/stacktest > out/stacktest.$1.out && echo SUCCESS || echo FAILURE
}

function run_memcached_one() {
	e=$1
	shift

	cnt=0
	while $TEST_MEMCACHED_PORT | fgrep -q 11211 > /dev/null; do
		[ $cnt -ge 3 ] && error "memcached port not free"
		cnt=$(expr $cnt + 1)
		sleep 1
	done

  FibrePollerCount=4 FibreStatsSignal=0 FibrePrintStats=t $TASKSET_SERVER $prog \
  -t $count -b 32768 -c 32768 -m 10240 -o hashpower=24 > out/memcached.$e.out & sleep 1

  timeout 10 mutilate -s0 --loadonly -r 100000 -K fb_key -V fb_value || error
  printf "RUN: %4s %5s %9s" $*

	[ "$(uname -s)" = "FreeBSD" ] && {
		RUN="$TASKSET_CLIENT" # pmcstat -P uops_retired.total_cycles -t '^memcached$'
	} || {
		RUN="$TASKSET_CLIENT perf stat -d --no-big-num -p $(pidof memcached) -o out/perf.$e.out"
	}

  $RUN timeout 30 \
  mutilate -s0 --noload -r 100000 -K fb_key -V fb_value -i fb_ia -t10 -u0.1 -T$clcnt $* \
  > out/mutilate.$e.out || error

  (echo stats;sleep 1)|telnet localhost 11211 \
  2> >(fgrep -v "Connection closed" 1>&2) \
  |fgrep lru_maintainer_juggles > out/juggles.$e.out || error

  killall memcached && sleep 1
}

function output_memcached() {
  e=$1
  shift
  lline=$(fgrep read out/mutilate.$e.out)
  lat=$(echo $lline|awk '{print $2}'|cut -f1 -d.)
  lvr=$(echo $lline|awk '{print $3}'|cut -f1 -d.)
  rline=$(fgrep rx out/mutilate.$e.out)
  rat=$(echo $rline|awk '{print $2}'|cut -f1 -d.)
  rvr=$(echo $rline|awk '{print $3}'|cut -f1 -d.)
  qline=$(fgrep "QPS" out/mutilate.$e.out)
  qps=$(echo $qline|awk '{print $4}'|cut -f1 -d.)
  req=$(echo $qline|cut -f2 -d'('|awk '{print $1}')
  jug=$(awk '{print $3}' out/juggles.$e.out)

	$precise && {
		printf " QPS: %7d RAT: %7d %7d LAT: %7d %7d" \
		$qps $rat $rvr $lat $lvr
	} || {
		printf " QPS: %4dK RAT: %4dK %4dK LAT: %4du %4du" \
		$(expr $qps / 1000) $(expr $rat / 1000) $(expr $rvr / 1000) $(expr $lat / 1000) $(expr $lvr / 1000)
	}

	[ "$(uname -s)" = "FreeBSD" ] || {
	  cyc=$(fgrep "cycles" out/perf.$e.out|fgrep -v stalled|awk '{print $1}')
	  ins=$(fgrep "instructions" out/perf.$e.out|awk '{print $1}')
	  l1c=$(fgrep "L1-dcache-load-misses" out/perf.$e.out|awk '{print $1}')
	  llc=$(fgrep "LLC-load-misses" out/perf.$e.out|awk '{print $1}')
	  cpu=$(fgrep "CPUs utilized" out/perf.$e.out|awk '{print $5}')

		[ "$l1c" = "<not" ] && l1c=0
		[ "$llc" = "<not" ] && llc=0

	  $precise && {
		  printf " L1C: %4d LLC: %4d INS: %6d CYC %6d CPU %7.3f" \
		  $(expr $l1c / $req) $(expr $llc / $req) $(expr $ins / $req) $(expr $cyc / $req) $cpu
	  } || {
		  printf " L1C: %4d LLC: %4d INS: %3dK CYC %3dK CPU %7.3f" \
		  $(expr $l1c / $req) $(expr $llc / $req) $(expr $ins / $req / 1000) $(expr $cyc / $req / 1000) $cpu
	  }
  }

  $precise && {
	  printf " jug: %6d\n" $jug
  } || {
	  printf " jug: %3dK\n" $(expr $jug / 1000)
	}
}

function run_memcached_all() {
	e=$1
  shift
	for d in 01 64; do
		for c in 025 500; do
			[ $# -gt 0 ] && { q=$1; shift; } || q=0
			run_memcached_one $e -d$d -c$c -q$q
			output_memcached $e
		done
	done
}

function run_memcached() {
	prog=memcached/memcached
	run_memcached_all $*
}

function run_vanilla() {
	prog=vanilla/memcached
	run_memcached_all $*
}

function prep_0() {
	echo vanilla
}

function prep_1() {
	echo threadtest
}

function prep_2() {
	[ "$(uname -s)" = "FreeBSD" ] && echo skip && return
	[ "$(uname -m)" = "aarch64" ] && echo skip && return
	echo stacktest
}

function prep_3() {
	echo memcached
}

function prep_4() {
	sed -i -e 's/.*#define TESTING_CLUSTER_POLLER_FIBRE .*/#undef TESTING_CLUSTER_POLLER_FIBRE/' src/runtime-glue/testoptions.h
	echo memcached
}

function prep_5() {
	sed -i -e 's/.*#define TESTING_CLUSTER_POLLER_FLOAT .*/#undef TESTING_CLUSTER_POLLER_FLOAT/' src/runtime-glue/testoptions.h
	sed -i -e 's/.*#define TESTING_EVENTPOLL_ONESHOT .*/#undef TESTING_EVENTPOLL_ONESHOT/' src/runtime-glue/testoptions.h
	echo memcached
}

function prep_6() {
	sed -i -e 's/.*#define TESTING_WORKER_POLLER .*/#define TESTING_WORKER_POLLER 1/' src/runtime-glue/testoptions.h
	sed -i -e 's/.*#define TESTING_CLUSTER_POLLER_FLOAT .*/#undef TESTING_CLUSTER_POLLER_FLOAT/' src/runtime-glue/testoptions.h
	sed -i -e 's/.*#define TESTING_EVENTPOLL_ONESHOT .*/#undef TESTING_EVENTPOLL_ONESHOT/' src/runtime-glue/testoptions.h
	sed -i -e 's/.*#define TESTING_EVENTPOLL_LEVEL .*/#define TESTING_EVENTPOLL_LEVEL 1/' src/runtime-glue/testoptions.h
	echo memcached
}

function prep_7() {
	sed -i -e 's/.*#define TESTING_LOADBALANCING .*/#undef TESTING_LOADBALANCING/' src/runtime/testoptions.h
	sed -i -e 's/.*#define TESTING_CLUSTER_POLLER_FIBRE .*/#undef TESTING_CLUSTER_POLLER_FIBRE/' src/runtime-glue/testoptions.h
	echo memcached
}

function prep_8() {
	sed -i -e 's/.*#define TESTING_IDLE_MANAGER .*/#undef TESTING_IDLE_MANAGER/' src/runtime/testoptions.h
	sed -i -e 's/.*#define TESTING_DEFAULT_AFFINITY .*/#undef TESTING_DEFAULT_AFFINITY/' src/runtime/testoptions.h
	echo memcached
}

function prep_9() {
	sed -i -e 's/.*#define TESTING_STUB_QUEUE .*/#define TESTING_STUB_QUEUE 1/' src/runtime/testoptions.h
	echo memcached
}

function prep_10() {
	[ "$(uname -s)" = "FreeBSD" ] && echo skip && return
	[ ! -f /usr/include/liburing.h ] && [ ! -f /usr/local/include/liburing.h ] && echo skip && return
	sed -i -e 's/.*#define TESTING_IO_URING .*/#define TESTING_IO_URING 1/' src/runtime-glue/testoptions.h
	echo memcached
}

function prep_11() {
	[ "$(uname -s)" = "FreeBSD" ] && echo skip && return
	[ ! -f /usr/include/liburing.h ] && [ ! -f /usr/local/include/liburing.h ] && echo skip && return
	sed -i -e 's/.*#define TESTING_IO_URING .*/#define TESTING_IO_URING 1/' src/runtime-glue/testoptions.h
	sed -i -e 's/.*#define TESTING_IO_URING_DEFAULT .*/#define TESTING_IO_URING_DEFAULT 1/' src/runtime-glue/testoptions.h
	echo memcached
}

function runexp() {
	$quiet || echo "========== RUNNING EXPERIMENT $1 =========="
	pre
  app=$(prep_$1)
  compile $app $1
  run_$app $*
  post
}

emax=11
quiet=false
precise=false

while getopts "pq" option; do
  case $option in
	p) precise=true;;
	q) quiet=true;;
	esac
done
shift $(($OPTIND - 1))

init

if [ $# -gt 0 ]; then
	if [ $1 -lt 0 -o $1 -gt $emax ]; then
		echo argument out of range
		exit 1
	fi
	trap post EXIT
	runexp $*
	exit 0
fi

if [ "$(uname -s)" = "Linux" ]; then
	$MAKE CC=clang all || exit 1
	$MAKE CC=clang clean || exit 1
fi

for ((e=1;e<=$emax;e+=1)); do
	trap "killall -9 memcached > /dev/null 2> /dev/null" EXIT
	runexp $e $*
done

exit 0
