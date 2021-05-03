#!/usr/bin/env bash

TYPES=(
  "p:pthread:FredMutex"
	"f:fifo:LockedMutex<WorkerLock, true>"
  "f:fast:FastMutex"
  "f:fibre:FredMutex"
  "f:simple:SimpleMutex0<false>"
  "f:direct:SimpleMutex0<true>"
)

function cleanup() {
	make clean > compile.out
	rm -f compile.out perf.out run.out
  git checkout apps/include/libfibre.h
  exit 1
}

trap cleanup SIGHUP SIGINT SIGQUIT SIGTERM

host=$(hostname)
if [ "$1" = "show" -o "$1" = "show1" -o "$1" = "show32" ]; then
	show=true
	case "$1" in
		show) fcnt="1024";;
		show1) fcnt="   1";;
		show32) fcnt="  32";;
	esac
	shift
	if ! [ $1 -eq $1 ] 2>/dev/null; then
		host=$1
		shift
	fi
else
	show=false
fi

for lcnt in $*; do
	if ! [ $lcnt -eq $lcnt ] 2>/dev/null; then
		echo "usage: $0 [show [<hostname>]] <lock count> ..."
	  exit 0
	fi
done

for lcnt in $*; do
	filename=locks.$lcnt.$host.out
	if $show; then
		for w in 1 10 100 1000 10000 100000; do
			grep "f: $fcnt w:.* $w "  locks.$lcnt.$host.out |sort -gr -k8
			echo
		done
		continue
	fi
	rm -f $filename
	for t in "${!TYPES[@]}"; do
		PREFIX=$(echo ${TYPES[$t]}|cut -f1 -d:)
		MUTEXNAME=$(echo ${TYPES[$t]}|cut -f2 -d:)
		MUTEXLOCK=$(echo ${TYPES[$t]}|cut -f3 -d:)
		sed -i -e "s/typedef FredMutex shim_mutex_t;/typedef ${MUTEXLOCK} shim_mutex_t;/" apps/include/libfibre.h
		echo "========== $lcnt locks / compiling ${MUTEXNAME} =========="
		make clean > compile.out
		make -j $(nproc) -C apps ${PREFIX}threadtest >> compile.out
		for w in 1 10 100 1000 10000 100000; do
			for f in 1 32 1024; do
				# perf stat -e task-clock --log-fd 1 -x,
				taskset -c 32-63 perf stat -e task-clock -o perf.out \
			  ./apps/${PREFIX}threadtest -l$lcnt -t32 -w$w -u$w -f$f | tee run.out
			  thr=$(cat run.out|fgrep loops/fibre|awk '{print $4}')
			  cpu=$(cat perf.out|fgrep "CPUs utilized"|awk '{print $5}')
			  printf "t: %7s f: %4d w: %6d o: %10d u: %6.3f\n" $MUTEXNAME $f $w $thr $cpu >> $filename
			done
		done
		sed -i -e "s/typedef ${MUTEXLOCK} shim_mutex_t;/typedef FredMutex shim_mutex_t;/" apps/include/libfibre.h
		rm -f compile.out perf.out run.out
	done
done
exit 0
