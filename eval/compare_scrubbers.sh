#!/bin/bash

timestamp=`date +%H%M%S_%d%b%y`
filename="scrubbers"$timestamp""
mlabfile="scrubbers"$timestamp".m"

seeds="22" # for more: "23 24 25 26 27 28 29 30 31"
#scrubarea="524288" # or 1Gb=2097152, 2Gb=4194304
capacity=$2
comments=$3

disk="/dev/$1"
filedir="/sys/block/$1/scrubber"
wldprog="/usr/src/measurements/seeker"
wldout="/usr/src/measurements/wldout.tmp"

runtime="60"
thinktime="1.0"

function calc {
	echo "scale=4; $1" | bc ;exit
}

function disable_disk_cache {
	# Disable on-disk cache and clear page- and filesystem-cache
	sudo sdparm --set WCE=0 --save $disk
	sudo sdparm --set RCD=1 --save $disk
}

function enable_disk_cache {
	# Enable on-disk cache and clear page- and filesystem-cache
	sudo sdparm --set WCE=1 --save $disk
	sudo sdparm --set RCD=0 --save $disk
}

function drop_page_caches {
	# Drop filesystem and page caches
	echo 3 > /proc/sys/vm/drop_caches
	echo 0 > /proc/sys/vm/drop_caches
}

function print_msg {
	# Print string on screen, results file, and .csv file
	echo -e $1
	echo -ne "% $1\n%\n" >> $mlabfile
	echo -ne "$1\n" >> $filename
}

function run_scrub {
	# ARGUMENTS:
	# $1 :: Experiment header string
	# $2 :: Matlab header string
	# $3 :: (0/1) On-disk cache
	# $4 :: (0/1) Drop page cache
	# $5 :: (0/1) O_DIRECT use
	# $6,$7 :: (type,priority) pair for kernel scrubber
	#          Types: seql, stag (or NONE to disable)
	#          Prios: realtime, idlechk, fixed
	# $8,$9 = (type,priority) pair for workload:
	#         Types: RAND, SEQL, SCRUB (or NONE to disable)
	#         Prios: DEF, IDLE, RT
	#
	# -- Workload Parameters --
	# $10 :: Starting point
	# $11 :: Sector count (area to run in)
	# $12 :: Run-time (in s)
	# $13 :: Think-time (probability)
	#
	# -- Scrubber Parameters --
	# $14 :: Starting point[s] (separated by spaces)
	# $15 :: Sector count (scrub area)
	# $16 :: Segment size (in Kb)
	# $17 :: Region size (in Kb)
	# $18 :: Threads
	# $19 :: Request bound
	# $20 :: Delay inbetween requests


	# Print header stuff
	print_msg "$1"
	echo -e "" >> $mlabfile

	# Define O_DIRECT flag
	if [ $5 -eq 0 ]; then
		dirflag=""
	else
		if [ $5 -eq 1 ]; then
			dirflag="-d"
		else
			echo "Warning: O_DIRECT flag needed state not well defined! (disabling...)"
			dirflag=""
		fi
	fi

	# Set on-disk cache
	if [ $3 -eq 0 ]; then
		disable_disk_cache
	else
		if [ $3 -eq 1 ]; then
			enable_disk_cache
		else
			echo "Warning: On-disk cache needed state not well defined! (enabling...)"
			enable_disk_cache
		fi
	fi

	# Initialize stat vars
	seedcount="0"
	partcount="0"
	if [ $6 != "NONE" ]; then
		Sreqrate="0"
		Sreqtime="0"
		Sreqnum="0"
		Scrtime="0"
	fi
	if [ $8 != "NONE" ]; then
		Wreqrate="0"
		Wreqtime="0"
		Wreqnum="0"
	fi

	for s in $seeds
	do
		seedcount=$(($seedcount+1))

		for p in ${14}
		do
			partcount=$(($partcount+1))

			# Dropping page cache. Or not.
			if [ $4 -eq 1 ]; then
				drop_page_caches
			else
				if [ $4 -ne 0 ]; then
					echo "Warning: Page cache needed state not well defined! (dropping...)"
					drop_page_caches
				fi
			fi

			# Defining scrubbing parameters
			if [ $6 != "NONE" ]; then
				sudo echo ${18} > $filedir/threads
				#echo "sudo echo ${18} > $filedir/threads"
				sudo echo $p > $filedir/spoint
				#echo "sudo echo $p > $filedir/spoint"
				sudo echo ${15} > $filedir/scount
				#echo "sudo echo ${15} > $filedir/scount"
				sudo echo on > $filedir/dpo
				sudo echo ${16} > $filedir/segsize
				#echo "sudo echo ${16} > $filedir/segsize"
				sudo echo ${17} > $filedir/regsize
				#echo "sudo echo ${17} > $filedir/regsize"
				sudo echo on > $filedir/timed
				sudo echo 1 > $filedir/verbose
				sudo echo ${6} > $filedir/strategy
				#echo "sudo echo ${6} > $filedir/strategy"
				sudo echo ${7} > $filedir/priority
				#echo "sudo echo ${7} > $filedir/priority"
				sudo echo 0 > $filedir/vrprotect
				sudo echo 0 > $filedir/ttime_ms
				sudo echo ${19} > $filedir/reqbound
				#echo "sudo echo ${19} > $filedir/reqbound"
				sudo echo ${20} > $filedir/delayms
				#echo "sudo echo ${20} > $filedir/delayms"
			fi

			if [ $8 != "NONE" ]; then
				# Start the workload executable(s) (add -o $fname for output)
				#if [ ${20} -le -4 ]; then
				#	fname="${2}${s}${partcount}_${timestamp}_rtX.dat"
				#else
				#	if [ "${20}" == "-2" ]; then
				#		fname="${2}${s}${partcount}_${timestamp}_rtCFQ.dat"
				#		fname="${2}${s}${partcount}_${timestamp}_rtX.dat"
				#	else
				#		fname="${2}${s}${partcount}_${timestamp}_rt${20}.dat"
				#	fi
				#fi

				sudo $wldprog -t 0.0 $dirflag -w $8 -p $9 -Z ${13} -P ${10} -S ${11} \
					-i exps.dat -s $s $disk > $wldout &
				wldpid=$!
			fi

			# Starting the scrubber
			if [ $6 != "NONE" ]; then
				# Start the scrubber
				sudo echo on > $filedir/state
				sudo echo off > $filedir/state
				reqtotal=`calc "${15} / (2.0 * ${16})"`
				flag=0
				echo -ne "  Awaiting scrubber response (  0%)"
				while [ $flag -eq 0 ]; do
					ttime="$(cat $filedir/ttime_ms)"
					reqcount="$(cat $filedir/reqcount)"
					if [ $ttime -gt 0 ]; then
						flag=1
					else
						sleep 5
						temp=$((`echo "scale=0; ($reqcount*100)/$reqtotal" | bc`))
						percent=`printf "%3d" $temp`
						echo -ne "\b\b\b\b\b${percent}%)"
					fi
				done
				reqcount="$(cat $filedir/reqcount)"
				rtime="$(cat $filedir/resptime_us)"
				echo -e "\b\b\b\b\b100%) ...Done in ${ttime}ms"
			else
				sleep ${12}
			fi

			if [ $8 != "NONE" ]; then
				# Terminate the workload executable(s)
				sudo kill -SIGINT $wldpid
				sleep 1

				read wrate wtime wnum < $wldout
				# Register results
				Wreqrate=`calc "$Wreqrate + $wrate"`
				Wreqtime=`calc "$Wreqtime + $wtime"`
				Wreqnum=`calc "$Wreqnum + $wnum"`
				echo "            Results (W$seedcount): Thp=$wrate, Rt=$wtime, Rq=$wnum." >> $filename

				#if [ "${20}" == "-1" ]; then
				#	echo -e "${2}CFQ_$(($partcount-1)) = load('${2}${s}${partcount}_${timestamp}_rtCFQ.dat')';" >> $mlabfile
				#else
				#	if [ "${20}" == "-2" ]; then
				#		echo -e "${2}X_$(($partcount-1)) = load('${2}${s}${partcount}_${timestamp}_rtX.dat')';" >> $mlabfile
				#	else
				#		echo -e "${2}${20}_$(($partcount-1)) = load('${2}${s}${partcount}_${timestamp}_rt${20}.dat')';" >> $mlabfile
				#	fi
				#fi
			fi

			if [ $6 != "NONE" ]; then
				srate=`calc "($reqcount * 1000) / $ttime"`
				stime=`calc "$rtime / 1000"`
				snum=$reqcount

				Sreqrate=`calc "$Sreqrate + $srate"`
				Sreqtime=`calc "$Sreqtime + $stime"`
				Sreqnum=`calc "$Sreqnum + $snum"`
				Scrtime=`calc "$Scrtime + $ttime"`
				echo "            Results (S$seedcount): Thp=$srate, Rt=$stime, Rq=$snum, Time=$ttime." >> $filename
			fi
		done
	done

	if [ $8 != "NONE" ]; then
		# Calculate results
		Wreqrate=`calc "$Wreqrate / ($seedcount * $partcount)"`
		Wreqtime=`calc "$Wreqtime / ($seedcount * $partcount)"`
		Wreqnum=`calc "$Wreqnum / ($seedcount * $partcount)"`

		echo -e "${2}W_thput = [${2}W_thput $Wreqrate];" >> $mlabfile
		echo -e "${2}W_rtime = [${2}W_rtime $Wreqtime];" >> $mlabfile
		echo -e "${2}W_count = [${2}W_count $Wreqnum];" >> $mlabfile
		echo -ne "      Results (W): ${Wreqrate}req/sec, ${Wreqtime}ms/req, ${Wreqnum}req.\n"
		echo -ne "      Results (W): ${Wreqrate}req/sec, ${Wreqtime}ms/req, ${Wreqnum}req.\n" >> $filename
	fi

	if [ $6 != "NONE" ]; then
		# Calculate results
		Sreqrate=`calc "$Sreqrate / ($seedcount * $partcount)"`
		Sreqtime=`calc "$Sreqtime / ($seedcount * $partcount)"`
		Sreqnum=`calc "$Sreqnum / ($seedcount * $partcount)"`
		Scrtime=`calc "$Scrtime / ($seedcount * $partcount)"`

		echo -e "${2}_stime = [${2}_stime $Scrtime];" >> $mlabfile
		echo -e "${2}S_thput = [${2}S_thput $Sreqrate];" >> $mlabfile
		echo -e "${2}S_rtime = [${2}S_rtime $Sreqtime];" >> $mlabfile
		echo -e "${2}S_count = [${2}S_count $Sreqnum];" >> $mlabfile
		echo -ne "      Results (S): ${Sreqrate}req/sec, ${Sreqtime}ms/req, ${Sreqnum}req.\n"
		echo -ne "      Results (S): ${Sreqrate}req/sec, ${Sreqtime}ms/req, ${Sreqnum}req.\n" >> $filename
	fi

	echo -e "" >> $mlabfile
}

function run_synthetic {
	# ARGUMENTS:
	# $1 :: Header to print
	# $2 :: (0/1) On-disk cache
	# $3 :: (0/1) Page cache drop
	# $4 :: (0/1) O_DIRECT use

	ss="64"
	scrubbers="seql stag"
	rs=`echo "scale=0; (($capacity/(512*2))/($ss*2))*($ss*2)" | bc`

	echo "Initiating comparison sequence ("$wl")..."
	echo -e "----------------------------------------\n"
	echo "Initiating comparison sequence ("$wl")..." >> $filename
	echo -e "----------------------------------------\n"  >> $filename

	print_msg "$1"
	segsizes="64 128 256 512 1024 2048 4096 8192 16384"
	regsizes=""
	for rt in 1 2 4 8 16 32 64 128 256 512; do
		temp=`echo "scale=0; $rt*$rs" | bc`
		regsizes="$regsizes $temp"
	done
	spoints="0"
	scrubarea=$capacity
	reqno="384000"
	for rt in 50 100 150 200 250 300 350 400 450; do
		temp=`echo "scale=0; $rt*$rs" | bc`
		spoints="$spoints $temp"
	done
	scrubarea_10=`echo "scale=0; $capacity/10" | bc`
	reqno_10=`echo "scale=0; $reqno/10" | bc`

	echo -e "SEQL_SS = []; STAG_SS = [];" >> $mlabfile
	echo -e "STAG_RS = [];" >> $mlabfile
	echo -e "SEQL_stime = []; SEQLS_thput = []; SEQLS_rtime = []; SEQLS_count = [];" >> $mlabfile
	echo -e "STGS_stime = []; STGSS_thput = []; STGSS_rtime = []; STGSS_count = [];" >> $mlabfile
	echo -e "STGR_stime = []; STGRS_thput = []; STGRS_rtime = []; STGRS_count = [];" >> $mlabfile

	#tempreq=$reqno_10
	#for ss in $segsizes
	#do
	#	echo -ne "SEQL_SS = [SEQL_SS $ss];\n" >> $mlabfile
	#	# Run scrubber by itself for $ss segsize
	#	run_scrub "SEQL by itself ($ss)" "SEQL" $2 $3 $4 "seql" "realtime" \
	#		"NONE" "DEF" 0 0 1 0 "$spoints" $scrubarea_10 $ss $rs 1 $tempreq 0
	#	tempreq=`echo "scale=0; $tempreq/2" | bc`
	#done
	#tempreq=$reqno
	#for ss in $segsizes
	#do
	#	echo -ne "STAG_SS = [STAG_SS $ss];\n" >> $mlabfile
	#	# Run scrubber by itself for $ss segsize
	#	run_scrub "STAG by itself ($ss)" "STGS" $2 $3 $4 "stag" "realtime" \
	#		"NONE" "DEF" 0 0 1 0 "0" $scrubarea $ss $rs 1 $tempreq 0
	#	tempreq=`echo "scale=0; $tempreq/2" | bc`
	#done
	ss="64"
	for rs in $regsizes
	do
		echo -ne "STAG_RS = [STAG_RS $rs];\n" >> $mlabfile
		# Run scrubber by itself for $rs regsize
		run_scrub "STAG by itself with region $rs" "STGR" $2 $3 $4 "stag" "realtime" \
			"NONE" "DEF" 0 0 1 0 "0" $scrubarea $ss $rs 1 $reqno 0
	done

	echo -e "" >> $mlabfile
	echo -e "----------------------------------------\n"
	echo -e "----------------------------------------\n"  >> $filename
}

echo -e "% "$comments"\n" > $mlabfile
echo -e $comments"\n" > $filename

#run_synthetic "Experiment 1: O_DIRECT + no on-disk cache" 0 1 1
run_synthetic "Experiment 2: O_DIRECT + on-disk cache" 1 1 1
#run_synthetic "Experiment 3: Normal reads (fs caches on) + on-disk cache" 1 1 0
#run_synthetic "Experiment 4: Normal reads (fs caches on) + NO on-disk cache" 0 1 0

echo -ne "\n\n"
echo -ne "\n\n" >> $filename

rm -v $wldout
