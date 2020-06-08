#!/bin/bash
[ $(id -u) -eq 0 ] && exec su -l kos -c "$0 $*"

if [ $(git diff | wc -l) -ne 0 ] && [ "$1" != "-f" ]; then
	echo "Uncommitted changes in repo; exiting..."
	exit 1
fi

count=$(expr $(nproc) / 4)
clbot1=0
cltop1=$(expr $count - 1)
clbot2=$(expr $count \* 2)
cltop2=$(expr $(expr $count \* 3) - 1)
svbot=$count
svtop=$(expr $(expr $count + $count) - 1)

echo server cores: $svbot-$svtop
echo client cores: $clbot1-$cltop1,$clbot2-$cltop2

function pre() {
	make clean all || exit 1
	[ "$1" = "memcached" ] || return
	FPATH=$PWD
	cd memcached; ./configure2.sh $FPATH; cd -
	make -C memcached all -j $(nproc) || exit 1
}

function post() {
	[ "$1" = "memcached" ] && make -C memcached distclean
	make clean
	git checkout src/runtime/testoptions.h
	git checkout src/runtime-glue/testoptions.h
}

function prep_0() {
	true
}

function run_0() {
	./apps/threadtest || exit 1
}

function prep_1() {
	sed -i -e 's/.*TESTING_PROCESSOR_POLLER.*/#define TESTING_PROCESSOR_POLLER 1/'
	echo memcached
}

function run_1() {
	touch memcached.running
	while [ -f memcached.running ]; do
		taskset -c $svbot-$svtop memcached/memcached -t $count -b 16384 -c 32768 -m 10240 -o hashpower=24
		sleep 1
	done | tee memcached.server.out &
	sleep 1
	for ((i=0;i<5;i+=1)); do
		mutilate -s0 -r 100000 -K fb_key -V fb_value --loadonly
		echo LOADED
		perf stat -p $(pidof memcached) taskset -c $cl1bot-$cl1top,$clbot2-$cltop2 \
		mutilate -s0 -r 100000 -K fb_key -V fb_value --noload -i fb_ia -u0.5 -c25 -d1 -t10 -T32
		[ $? -eq 0 ] && killall memcached || {
			rm -f memcached.running
			killall memcached
			exit 1
		}
		sleep 3
	done | tee memcached.client.out
	rm -f memcached.running
}

for ((e=0;e<2;e+=1)); do
	addon=$(prep_$e)
	pre $addon
	run_$e
	post $addon
done
exit 0
