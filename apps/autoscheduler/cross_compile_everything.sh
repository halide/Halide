#!/bin/bash

# crosscompile all autotuned apps for arm-64-linux
# autoscheduled get two variants, beamsize=1 and beamsize=32
#
# results are placed in bin/crosscompiled/*

set -eu

usage()
{
    echo "Usage: cross_compile_everything.sh generate [/path/to/output]"
    echo "       cross_compile_everything.sh reassemble 32|1 [/path/to/input]"
}

if [ $# -eq 0 ]; then
    usage
    exit 1
fi

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
APPS_DIR=${SCRIPT_DIR}/..

TARG=${HL_TARGET:=arm-64-linux}

echo Using target ${HL_TARGET}

# APPDIR;GENERATOR;SUBDIR;MANUAL;SUFFIX
declare -a INFO=( \
  "bgu;fit_and_slice_3x4;;" \
  "bilateral_grid;bilateral_grid;;" \
  "blur;halide_blur;${TARG}/;;" \
  "burst_camera_pipe;burst_camera_pipe;;" \
  "camera_pipe;camera_pipe;;" \
  "conv_layer;conv_layer;;" \
  "harris;harris;;" \
  "hist;hist;;" \
  "iir_blur_generator;iir_blur;;" \
  "interpolate_generator;interpolate;;" \
  "lens_blur;lens_blur;;" \
  "local_laplacian;local_laplacian;;" \
  "mat_mul_generator;mat_mul;;" \
  "max_filter;max_filter;;" \
  "nl_means;nl_means;;" \
  "stencil_chain;stencil_chain;;" \
  "unsharp;unsharp;;" \
)

for i in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
    INFO+=("resnet_50;resnet50block;;_manual;$i")
done

if [ "$1" = "generate" ]; then

    export AUTOSCHED_WEIGHTS_DIR=${SCRIPT_DIR}/arm_weights

    if [ $(uname -s) = "Darwin" ]; then
        LOCAL_CORES=`sysctl -n hw.ncpu`
    else
        LOCAL_CORES=`nproc`
    fi
    echo Local number of cores detected as ${LOCAL_CORES}

    if [ $# -ge 2 ]; then
        DST_DIR=$2
    else
        DST_DIR=${SCRIPT_DIR}/bin/crosscompiled
    fi

    echo Generating to ${DST_DIR}...
    mkdir -p ${DST_DIR}

    cd ${APPS_DIR}/autoscheduler

    # Make this first so subtasks won't get into contention over it
    make ../autoscheduler/bin/libauto_schedule.so

    wait_for_core()
    {
        while [[ 1 ]]; do
            RUNNING=$(jobs -r | wc -l)
            if [[ RUNNING -ge LOCAL_CORES ]]; then
                sleep 1
            else
                break
            fi
        done
    }

    for i in ${INFO[@]}; do
        IFS=';' read -r -a fields <<< "${i}"

        APPDIR=${fields[0]}

        rm -rf ${APPS_DIR}/${APPDIR}/bin ${APPS_DIR}/${APPDIR}/bin1
        mkdir -p ${APPS_DIR}/${APPDIR}/bin
        mkdir -p ${APPS_DIR}/${APPDIR}/bin1
    done

    for i in ${INFO[@]}; do
        IFS=';' read -r -a fields <<< "${i}"

        APPDIR=${fields[0]}
        GENERATOR=${fields[1]}
        if [ ${#fields[@]} -le 2 ]; then
            SUBDIR=
        else
            SUBDIR=${fields[2]}
        fi
        if [ ${#fields[@]} -le 3 ]; then
            MANUAL=
        else
            MANUAL=${fields[3]}
        fi
        if [ ${#fields[@]} -le 4 ]; then
            SUFFIX=
        else
            SUFFIX=${fields[4]}
        fi

        mkdir -p ${DST_DIR}/${APPDIR}

        cd ${APPS_DIR}/${APPDIR}

        wait_for_core
        # This is synchronous; must be sure it's build before firing off the invocations,
        # to avoid bg task contention
        echo Building ${APPDIR}/${GENERATOR}.generator...
        BIN=bin HL_TARGET=arm-64-linux make bin/${GENERATOR}.generator &> /dev/null
        cp -n bin/${GENERATOR}.generator bin1/

        wait_for_core
        echo Starting ${APPDIR}/${GENERATOR}${SUFFIX} 1...
        BIN=bin HL_TARGET=arm-64-linux make bin/${SUBDIR}${GENERATOR}${MANUAL}${SUFFIX}.a &> /dev/null && \
            echo Finishing ${APPDIR}/${GENERATOR} 1... && \
            mv bin/${SUBDIR}${GENERATOR}${MANUAL}${SUFFIX}*.{a,h,registration.cpp} ${DST_DIR}/${APPDIR}/ &

        wait_for_core
        echo Starting ${APPDIR}/${GENERATOR}${SUFFIX} 2...
        BIN=bin HL_TARGET=arm-64-linux make bin/${SUBDIR}${GENERATOR}_classic_auto_schedule${SUFFIX}.a &> /dev/null && \
            echo Finishing ${APPDIR}/${GENERATOR} 2... && \
            mv bin/${SUBDIR}${GENERATOR}_classic_auto_schedule${SUFFIX}.{a,h,registration.cpp} ${DST_DIR}/${APPDIR}/ &

        # Emit beamsize=32 into normal bin, rename afterward
        wait_for_core
        echo Starting ${APPDIR}/${GENERATOR}${SUFFIX} 3...
        BIN=bin HL_BEAM_SIZE=32 HL_TARGET=arm-64-linux make bin/${SUBDIR}${GENERATOR}_auto_schedule${SUFFIX}.a &> /dev/null && \
            for f in bin/${SUBDIR}${GENERATOR}_auto_schedule${SUFFIX}.*; do mv "$f" "${f/_auto_schedule/_beamsize32_auto_schedule}"; done && \
            echo Finishing ${APPDIR}/${GENERATOR} 3... && \
            mv bin/${SUBDIR}${GENERATOR}_beamsize32_auto_schedule${SUFFIX}.{a,h,registration.cpp} ${DST_DIR}/${APPDIR}/ &

        # Emit beamsize=1 into bin1, rename afterward
        wait_for_core
        echo Starting ${APPDIR}/${GENERATOR}${SUFFIX} 4...
        BIN=bin1 HL_BEAM_SIZE=1 HL_TARGET=arm-64-linux make bin1/${SUBDIR}${GENERATOR}_auto_schedule${SUFFIX}.a &> /dev/null && \
            for f in bin1/${SUBDIR}${GENERATOR}_auto_schedule${SUFFIX}.*; do mv "$f" "${f/_auto_schedule/_beamsize1_auto_schedule}"; done && \
            echo Finishing ${APPDIR}/${GENERATOR} 4... && \
            mv bin1/${SUBDIR}${GENERATOR}_beamsize1_auto_schedule${SUFFIX}.{a,h,registration.cpp} ${DST_DIR}/${APPDIR}/ &

    done

    wait

    echo Done.

elif [ "$1" = "reassemble" ]; then

    if [ $# -ge 2 ]; then
        BEAMSIZE=$2
    else
        usage
        exit 1
    fi

    if [ $BEAMSIZE -ne 1 -a $BEAMSIZE -ne 32 ]; then
        usage
        exit 1
    fi

    if [ $# -ge 3 ]; then
        SRC_DIR=$3
    else
        SRC_DIR=${SCRIPT_DIR}/bin/crosscompiled
    fi
    echo Reassembling from ${SRC_DIR}...

    for i in ${INFO[@]}; do
        IFS=';' read -r -a fields <<< "${i}"

        APPDIR=${fields[0]}
        GENERATOR=${fields[1]}
        if [ ${#fields[@]} -le 2 ]; then
            SUBDIR=
        else
            SUBDIR=${fields[2]}
        fi
        if [ ${#fields[@]} -le 3 ]; then
            MANUAL=
        else
            MANUAL=${fields[3]}
        fi
        if [ ${#fields[@]} -le 4 ]; then
            SUFFIX=
        else
            SUFFIX=${fields[4]}
        fi

        DST_DIR=${APPS_DIR}/${APPDIR}/bin
        rm -rf ${DST_DIR}
        mkdir -p ${DST_DIR}
        cp ${SRC_DIR}/${APPDIR}/${GENERATOR}${MANUAL}${SUFFIX}*.{a,h,registration.cpp} ${DST_DIR}/
        cp ${SRC_DIR}/${APPDIR}/${GENERATOR}_classic_auto_schedule${SUFFIX}.{a,h,registration.cpp} ${SCRIPT_DIR}/${APPDIR}/
        cp ${SRC_DIR}/${APPDIR}/${GENERATOR}_beamsize${BEAMSIZE}_auto_schedule${SUFFIX}.{a,h,registration.cpp} ${DST_DIR}/${APPDIR}/
        for f in ${DST_DIR}/${APPDIR}/_auto_schedule${SUFFIX}.*; do echo mv "$f" "${f/_beamsize${BEAMSIZE}_auto_schedule/_auto_schedule}"; done
    done

else
    usage
    exit 1
fi

