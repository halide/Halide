#!/bin/bash

# Run tflite model on bexagon-sim
# Usage:
#     ./run_hexagon_sim.sh <tflite-model>

BIN=${BIN:-bin}

$DEFAULT_HEXAGON_TOOLS_ROOT/Tools/bin/hexagon-sim \
    -mv65 \
    --usefs=$DEFAULT_HEXAGON_TOOLS_ROOT/Tools/target/hexagon/lib/v65/G0/pic \
    --simulated_returnval \
    --cosim_file $BIN/$HL_TARGET/q6ss.cfg \
    --l2tcm_base 0xd800 \
    --rtos $BIN/$HL_TARGET/osam.cfg \
    $HEXAGON_SDK_ROOT/rtos/qurt/computev65/sdksim_bin/runelf.pbn -- \
    $HEXAGON_SDK_ROOT/libs/run_main_on_hexagon/ship/hexagon_toolv84_v65/run_main_on_hexagon_sim \
    stack_size=0x400000 -- \
    $PWD/$BIN/$HL_TARGET/$BENCHMARK_OUT --verbose $1;
