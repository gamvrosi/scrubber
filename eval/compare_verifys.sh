#!/bin/bash

timestamp=`date +%H%M%S_%d%b%y`
filename="verifys"$timestamp".m"

segsizes="1 2 4 8 16 32 64 128 256 512 1024 2048 4096 8192 16384" # and 32767
threads="1"
filedir="/sys/block/$1/scrubber"

function calc {
	echo "scale=4; $1" | bc ;exit
}

echo "Starting Experiments for $1 (to find verify times)"
echo -e "-----------------------------------------------\n"
echo "% Starting Experiments for $1 (to find verify times)" > $filename
echo -e "% -----------------------------------------------\n" >> $filename

echo "SS = [];" >> $filename
echo "TIME = [];" >> $filename

for ss in $segsizes
do
	echo "SS = [SS "$ss"];" >> $filename
	rtime="0"
	ttime="0"
	echo "%  Using segment size: "$ss" Kbytes" >> $filename
	sudo echo $threads > $filedir/threads
	sudo echo on > $filedir/dpo
	sudo echo $ss > $filedir/segsize
	sudo echo on > $filedir/timed
	sudo echo 1 > $filedir/verbose
	sudo echo fixed > $filedir/strategy
	sudo echo idlechk > $filedir/priority
	sudo echo 0 > $filedir/vrprotect
	sudo echo 0 > $filedir/ttime_ms
	sudo echo 0 > $filedir/reqcount
	sudo echo 0 > $filedir/delayms
	# Start the scrubber
	sudo echo on > $filedir/state
	sudo echo off > $filedir/state
	flag=0
	sleep 1
	echo -ne "- Trying with Segment size: "$ss" Kbytes... "
	while [ $flag -eq 0 ]; do
		ttime="$(cat $filedir/ttime_ms)"
		if [ $ttime -gt 0 ]; then
			flag=1
		else
			sleep 1
		fi
	done
	rtime="$(cat $filedir/resptime_us)"
	echo "Done in ${ttime}ms"

	echo "TIME = [TIME "$rtime"/1000.0];" >> $filename # that should be msec
done
