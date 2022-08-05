#!/bin/bash

LOGFILE=${1}
TIMEOUT=${2:-10}
echo "Starting watchdog" | tee -a $LOGFILE
while [ 1 ]; do
    sleep $TIMEOUT
    CMD="ps -ax | grep 'bench --estimate_all' | grep -v grep | cut -dp -f1 | cut -d? -f1"
    RES=$(eval $CMD)
    if [ ! -z "$RES" ]; then
        echo watchdog: killing $RES - waited for $TIMEOUT seconds | tee -a $LOGFILE
        echo $RES | xargs kill 2>/dev/null
    fi
done

