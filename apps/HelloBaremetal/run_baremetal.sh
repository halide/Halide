#!/bin/bash
set -euo pipefail

# Prerequisite :
#   QEMU Arm System Emulator is installed (https://www.qemu.org/docs/master/system/target-arm.html)

if [ $# -eq 0 ]; then
    echo "Usage: run_baremetal.sh executable [arg]..."
    exit 1
fi

EXECUTABLE="$1"
EXE_ARGS="$*"

QEMU_MACHINE_ARGS="-M realview-pbx-a9 -cpu cortex-a9 -smp 1 -m 1024M"

echo "Running command:  $EXE_ARGS"

qemu-system-arm \
    ${QEMU_MACHINE_ARGS} \
    -monitor null -serial null -nographic \
    -kernel ${EXECUTABLE} \
    -semihosting -semihosting-config enable=on,target=native,arg="${EXE_ARGS}"

echo "Exit from QEMU"
