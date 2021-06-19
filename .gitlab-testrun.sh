#!/usr/bin/env bash
[ $(id -u) -eq 0 ] && exec su -l kos -c "$0 $*"

if [ "$(uname -s)" = "FreeBSD" ]; then
	MAKE=gmake
	count=$(expr $(sysctl kern.smp.cpus|cut -c16- || echo 1) / 4)
	[ $count -gt 0 ] || count=1
	clbot=0
	cltop=$(expr $count \* 2 - 1)
	svmax=$(expr $count \* 4)
	svlist=$(expr $count \* 2)
	for ((c=$svlist+2;c<$svmax;c+=2)); do
		svlist=$svlist,$c
	done
	echo server cores: $svlist
	echo client cores: $clbot-$cltop
	TASKSET_SERVER="cpuset -l $svlist time -p"
	TASKSET_CLIENT="cpuset -l $clbot-$cltop"
	# pmc or pmcstat?
	PERF_SERVER=""
else
	MAKE=make
	count=$(expr $(nproc) / 4)
	[ $count -gt 0 ] || count=1
	clbot1=0
	cltop1=$(expr $count - 1)
	clbot2=$(expr $count \* 2)
	cltop2=$(expr $count \* 3 - 1)
	svbot=$count
	svtop=$(expr $count \* 2 - 1)
	echo server cores: $svbot-$svtop
	echo client cores: $clbot1-$cltop1,$clbot2-$cltop2
	TASKSET_SERVER="taskset -c $svbot-$svtop"
	TASKSET_CLIENT="taskset -c $clbot1-$cltop1,$clbot2-$cltop2"
	PERF_SERVER="perf stat -p \$(pidof memcached)"
fi

function pre() {
	[ "$1" = "skip" ] && return
	$MAKE clean all || exit 1
	[ "$1" = "memcached" ] || return
	FPATH=$PWD
	cd memcached; ./configure2.sh $FPATH; cd -
	$MAKE -C memcached all -j $count || exit 1
}

function post() {
	[ "$1" = "memcached" ] && $MAKE -C memcached distclean
	$MAKE clean
	git checkout Makefile.config
	git checkout src/runtime/testoptions.h
	git checkout src/runtime-glue/testoptions.h
}

function run_threadtest() {
	c=$(expr $count \* 2)
	FibrePrintStats=1 ./apps/threadtest -t $c -f $(expr $c \* $c) || exit 1
	FibrePrintStats=1 ./apps/threadtest -t $c -f $(expr $c \* $c) -o 10000 -r -L T || exit 1
}

function run_stacktest() {
	[ "$(uname -s)" = "FreeBSD" ] && return
	echo -n "STACKTEST: "
	FibrePrintStats=1 apps/stacktest > stacktest.$1.out && echo SUCCESS || echo FAILURE
}

function run_memcached() {
	for ((i=0;i<3;i+=1)); do
		FibrePrintStats=1 $TASKSET_SERVER memcached/memcached -t $count -b 16384 -c 32768 -m 10240 -o hashpower=24 &
		sleep 3
		mutilate -s0 -r 100000 -K fb_key -V fb_value --loadonly
		echo LOADED
		eval $PERF_SERVER $TASKSET_CLIENT timeout 30 \
		mutilate -s0 -r 100000 -K fb_key -V fb_value --noload -i fb_ia -u0.5 -c25 -d1 -t10 -T$(expr $count \* 2)
		result=$?
		killall memcached
		wait
		[ $result -eq 0 ] || exit 1
	done | tee memcached.$1.out
}

function prep_0() {
	echo threadtest
}

function prep_1() {
	[ "$(uname -s)" = "FreeBSD" ] && echo skip && return
	[ "$(uname -m)" = "aarch64" ] && echo skip && return
	sed -i -e 's/DYNSTACK?=.*/DYNSTACK?=1/' Makefile.config
	echo stacktest
}

function prep_2() {
	echo memcached
}

function prep_3() {
	sed -i -e 's/.* TESTING_WORKER_POLLER .*/#define TESTING_WORKER_POLLER 1/' src/runtime-glue/testoptions.h
	echo memcached
}

function prep_4() {
	sed -i -e 's/.* TESTING_CLUSTER_POLLER_FIBRE .*/#undef TESTING_CLUSTER_POLLER_FIBRE/' src/runtime-glue/testoptions.h
	echo memcached
}

function prep_5() {
	sed -i -e 's/.* TESTING_CLUSTER_POLLER_FLOAT .*/#undef TESTING_CLUSTER_POLLER_FLOAT/' src/runtime-glue/testoptions.h
	sed -i -e 's/.* TESTING_ONESHOT_REGISTRATION .*/#undef TESTING_ONESHOT_REGISTRATION/' src/runtime-glue/testoptions.h
	echo memcached
}

function prep_6() {
	sed -i -e 's/.* TESTING_WORKER_POLLER .*/#define TESTING_WORKER_POLLER 1/' src/runtime-glue/testoptions.h
	sed -i -e 's/.* TESTING_CLUSTER_POLLER_FIBRE .*/#undef TESTING_CLUSTER_POLLER_FIBRE/' src/runtime-glue/testoptions.h
	sed -i -e 's/.* TESTING_ONESHOT_REGISTRATION .*/#define TESTING_ONESHOT_REGISTRATION 1/' src/runtime-glue/testoptions.h
	echo memcached
}

function prep_7() {
	sed -i -e 's/.* TESTING_LOADBALANCING .*/#undef TESTING_LOADBALANCING/' src/runtime/testoptions.h
	sed -i -e 's/.* TESTING_CLUSTER_POLLER_FIBRE .*/#undef TESTING_CLUSTER_POLLER_FIBRE/' src/runtime-glue/testoptions.h
	echo memcached
}

function prep_8() {
	sed -i -e 's/.* TESTING_IDLE_MANAGER .*/#undef TESTING_IDLE_MANAGER/' src/runtime/testoptions.h
	sed -i -e 's/.* TESTING_DEFAULT_AFFINITY .*/#undef TESTING_DEFAULT_AFFINITY/' src/runtime/testoptions.h
	echo memcached
}

function prep_9() {
	sed -i -e 's/.* TESTING_STUB_QUEUE .*/#define TESTING_STUB_QUEUE 1/' src/runtime/testoptions.h
	echo memcached
}

emax=9

if [ $# -gt 0 ] && [ "$1" != "-f" ]; then
	if [ $1 -lt 0 -o $1 -gt $emax ]; then
		echo argument out of range
		exit 1
	fi
	app=$(prep_$1)
	pre $app
	run_$app $1
	$MAKE -C memcached distclean
	$MAKE clean
	exit 0
fi

if [ "$(uname -s)" = "Linux" ]; then
	$MAKE CC=clang all || exit 1
	$MAKE CC=clang clean || exit 1
fi

if [ $(git diff | wc -l) -ne 0 ] && [ "$1" != "-f" ]; then
	echo "Uncommitted changes in repo; exiting..."
	exit 1
fi

for ((e=0;e<=$emax;e+=1)); do
	echo "========== RUNNING EXPERIMENT $e =========="
	app=$(prep_$e)
	pre $app
	run_$app $e
	post $app
done
exit 0
