#!/bin/bash

#./compare_scrubbers.sh sde 585937500
#./compare_scrubbers.sh sdf 143374738

./compare_workloads.sh sde 585937500 "64Kb/64Kb on /dev/sde for 128MB" # 4194304 sectors"
./compare_workloads.sh sdf 143374738 "64Kb/64Kb on /dev/sdf for 128MB" # 4194304 sectors"

