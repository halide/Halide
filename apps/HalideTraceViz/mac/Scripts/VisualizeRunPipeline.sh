#!/bin/bash

# VisualizeRunPipeline.sh
# mPerpetuo, inc.
# Jaime Rios

set -e
set -u
set -o errexit

function BuildBinaries()
{
    echo "${FUNCNAME[0]}"
    cd ..

    xcodebuild -project HalideTraceViz.xcodeproj -scheme HalideTraceViz \
    CONFIGURATION_BUILD_DIR=build/Debug -configuration "Debug" clean build

    xcodebuild -project HalideTraceViz.xcodeproj -scheme MakePipeline \
    CONFIGURATION_BUILD_DIR=build/Debug -configuration "Debug" clean build

    # Since libHalide.so is not compiled with @rpath enabled, we have to copy
    # the binary local to the MakePipeline binary
    cp -vR components/halide/bin build/Debug/

    # Create .o file with HALIDE_TRACE flag turned on for RunPipeline to link
    # against
    cd build/Debug
    HL_TRACE=3 ./MakePipeline
    cd ../..

    xcodebuild -project HalideTraceViz.xcodeproj -scheme RunPipeline \
    CONFIGURATION_BUILD_DIR=build/Debug -configuration "Debug" clean build


    cd $CURRENT_PATH
}

function CopyBinaries()
{
    echo "${FUNCNAME[0]}"

    if [[ -d ./VisualizePipeline ]]; then
        rm -Rv ./VisualizePipeline
    fi
    mkdir VisualizePipeline

    cp -Rv ../build/Debug ./VisualizePipeline/Debug
}

# Function simply checks our required binary files and fails if 
# all of them are not in place
function CheckForRequiredBinaries()
{
    echo "${FUNCNAME[0]} started"

    cd $CURRENT_PATH/VisualizePipeline/Debug
    if [[ ! -f MakePipeline ]]; then
        echo "MakePipeline not found!"
        exit 1
    fi

    if [[ ! -f RunPipeline ]]; then
        echo "RunPipeline not found!"
        exit 1
    fi

    if [[ ! -f HalideTraceViz ]]; then
        echo "HalideTraceViz not found!"
        exit 1
    fi

    cd $CURRENT_PATH/..
    if [[ ! -f components/ffmpeg/ffmpeg ]]; then
        echo "ffmpeg not found!"
        exit 1
    fi

    echo "${FUNCNAME[0]} completed successfully"
    cd $CURRENT_PATH
}

function VisualizeFunctions()
{
    echo "${FUNCNAME[0]}"

    local BLANK=0
    local ZOOM=8
    local COST=1

    local STRIDE0="1 0"
    local STRIDE1="0 1"

    if [[ ! -d movies ]]; then
        mkdir movies
    fi

    cd $CURRENT_PATH/VisualizePipeline/Debug

    HL_TRACE=3 ./MakePipeline && \
    HL_TRACE_FILE=/dev/stdout ./RunPipeline | \
    ./HalideTraceViz -s 1290 1024 -t 1 -d 100 \
\
    -l input Input 2 24 1 \
    -l output Onput 516 24 1 \
\
    -f input 0 127 -1 $BLANK $ZOOM $COST 0 26 $STRIDE0 $STRIDE1 \
    -f output 1 128 -1 $BLANK $ZOOM $COST 516 26 $STRIDE0 $STRIDE1 | \
    ../../../components/ffmpeg/ffmpeg -r 30 -f rawvideo -pix_fmt bgra \
    -s 1290X1024  -i - -y -pix_fmt yuv420p ../../movies/Brighten_schedule.mp4

    cd $CURRENT_PATH
}

echo "Starting ${0##*/}"
CURRENT_PATH=$PWD

BuildBinaries
CopyBinaries
CheckForRequiredBinaries
VisualizeFunctions

echo "All done"