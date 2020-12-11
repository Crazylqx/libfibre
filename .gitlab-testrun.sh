#!/usr/bin/env bash
[ $(id -u) -eq 0 ] && exec su -l kos -c "$0 $*"

if [ $(git diff | wc -l) -ne 0 ] && [ "$1" != "-f" ]; then
	echo "Uncommitted changes in repo; exiting..."
	exit 1
fi

if [ "$(uname -s)" = "FreeBSD" ]; then
	MAKE=gmake
	count=$(expr $(sysctl kern.smp.cpus|cut -c16- || echo 1) / 4)
	[ $count -gt 0 ] || count=1
else
	MAKE=make
	count=$(expr $(nproc) / 4)
	[ $count -gt 0 ] || count=1
	clbot1=0
	cltop1=$(expr $count - 1)
	clbot2=$(expr $count \* 2)
	cltop2=$(expr $(expr $count \* 3) - 1)
	svbot=$count
	svtop=$(expr $(expr $count + $count) - 1)
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

function run_memcached() {
	touch memcached.running
	while [ -f memcached.running ]; do
		$TASKSET_SERVER memcached/memcached -t $count -b 16384 -c 32768 -m 10240 -o hashpower=24
		sleep 1
	done | tee memcached.server.$1.out &
	for ((i=0;i<5;i+=1)); do
		sleep 3
		mutilate -s0 -r 100000 -K fb_key -V fb_value --loadonly
		echo LOADED
		eval $PERF_SERVER $TASKSET_CLIENT timeout 30 \
		mutilate -s0 -r 100000 -K fb_key -V fb_value --noload -i fb_ia -u0.5 -c25 -d1 -t10 -T32
		[ $? -eq 0 ] && killall memcached || {
			rm -f memcached.running
			killall memcached
			sleep 1
			exit 1
		}
	done | tee memcached.client.$1.out
	rm -f memcached.running
}

function prep_0() {
	echo memcached
}

function run_0() {
	./apps/threadtest || exit 1
	run_memcached 0
}

function prep_1() {
	[ "$(uname -s)" = "FreeBSD" ] && echo skip && return
	sed -i -e 's/DYNSTACK=.*/DYNSTACK=1/' Makefile.config
}

function run_1() {
	[ "$(uname -s)" = "FreeBSD" ] && return
	echo -n "STACKTEST: "
	apps/stacktest > stacktest.out && echo SUCCESS || echo FAILURE
}

function prep_2() {
	sed -i -e 's/.*#define TESTING_PROCESSOR_POLLER.*/#define TESTING_PROCESSOR_POLLER 1/' src/runtime-glue/testoptions.h
	echo memcached
}

function run_2() {
	run_memcached 2
}

function prep_3() {
	sed -i -e 's/.*#define TESTING_CLUSTER_POLLER_FIBRE.*/#undef TESTING_CLUSTER_POLLER_FIBRE/' src/runtime-glue/testoptions.h
	echo memcached
}

function run_3() {
	run_memcached 3
}

function prep_4() {
	sed -i -e 's/.*#define TESTING_LAZY_FD_REGISTRATION.*/#undef TESTING_LAZY_FD_REGISTRATION/' src/runtime-glue/testoptions.h
	echo memcached
}

function run_4() {
	run_memcached 4
}

function prep_5() {
	sed -i -e 's/.*#define TESTING_PROCESSOR_POLLER.*/#define TESTING_PROCESSOR_POLLER 1/' src/runtime-glue/testoptions.h
	sed -i -e 's/.*#define TESTING_CLUSTER_POLLER_FIBRE.*/#undef TESTING_CLUSTER_POLLER_FIBRE/' src/runtime-glue/testoptions.h
	sed -i -e 's/.*#define TESTING_LAZY_FD_REGISTRATION.*/#undef TESTING_LAZY_FD_REGISTRATION/' src/runtime-glue/testoptions.h
	echo memcached
}

function run_5() {
	run_memcached 5
}

for ((e=0;e<=5;e+=1)); do
	addon=$(prep_$e)
	pre $addon
	run_$e
	post $addon
done
exit 0
