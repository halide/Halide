#!/bin/bash

# This is a trivial script to try running 'classic' autoscheduler
# with the multiple variants of MachineParams used in the original paper;
# all it does is disable benchmarking of non-classic variants, and run 'make test'
# in the current directory repeatedly with different values (removing
# any classic artifacts from bin to ensure a rebuild occurs).
#
# To use:
# - cd to the dir of the app you want to bench
# - run this script

set -eu

export HL_THREE_WAY_BENCH_SKIP_MANUAL=1
export HL_THREE_WAY_BENCH_SKIP_AUTO_CLASSIC=0
export HL_THREE_WAY_BENCH_SKIP_AUTO_NEW=1

# Ensure that building the non-classic variants doesn't flood us with noise
echo Prebuilding...
make test &> /dev/null

CORES=32
for CACHE in 12000000 24000000 48000000; do
    for BALANCE in 80 160 320; do
        rm -f bin/*_classic_auto_schedule*

        echo
        echo Benchmarking with HL_MACHINE_PARAMS=${CORES},${CACHE},${BALANCE}...
        HL_MACHINE_PARAMS=${CORES},${CACHE},${BALANCE} make test 2>/dev/null | grep ".*time:.*"
    done
done

echo Done.
