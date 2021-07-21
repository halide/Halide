#!/bin/bash
#
# Run the compare_vs_tflite binary on device. This script will build and push the
# binary to the device, push the tflite file(s), the run the binary.
#
# export ANDROID_SERIAL as an env var to choose a specific device (if you have multiple connected via adb).
#
# export HL_TARGET to specify the target architecture to build. (Defaults to arm-64-android.)
#
# usage: HL_TARGET=arm-64-android ./run_compare_on_device.sh local_testdata/*.tflite --flag1 --flag2 [--cmake] ...

set -e

HANNK_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

export HL_TARGET=${HL_TARGET:-arm-64-android}

TENSORFLOW_BASE=${TENSORFLOW_BASE:-${HOME}/GitHub/tensorflow}
TFLITE_SHARED_LIBRARY=${TFLITE_SHARED_LIBRARY:-${TENSORFLOW_BASE}/tflite_build_android/libtensorflowlite_c.so}

DEVICE_DIR=/data/local/tmp/halide/compare_vs_tflite

if [[ -n "${ANDROID_SERIAL}" ]]; then
    echo Using ANDROID_SERIAL=${ANDROID_SERIAL}
fi
if [[ -n "${TASKSET}" ]]; then
  echo Using TASKSET=${TASKSET}
  TASKSET_CMD="taskset ${TASKSET}"
fi
echo Using HL_TARGET=${HL_TARGET}

if [ "$#" -eq 0 ]; then
    echo "Specify at least one .tflite file to use."
    exit 1
fi

LOCAL_FILES=
DEVICE_ARGS=
BUILD_IS_CMAKE=0
for ARG in "$@"
do
    if [[ "${ARG}" == "--cmake" ]]; then
        # Don't propagate
        BUILD_IS_CMAKE=1
    else
        if [ -f "${ARG}" ]; then
            # assume it's a file.
            BASENAME=$(basename "${ARG}")
            LOCAL_FILES="${LOCAL_FILES} ${ARG}"
            DEVICE_ARGS="${DEVICE_ARGS} ${DEVICE_DIR}/${BASENAME}"
        else
            # assume it's a flag (or flag argument) and just pass thru
            DEVICE_ARGS="${DEVICE_ARGS} ${ARG}"
        fi
    fi
done

if [[ ${BUILD_IS_CMAKE} -eq 1 ]]; then
    # TODO: this isn't working yet; crosscompilation in CMake is painful
    echo Building [CMake]...
    ${HANNK_DIR}/configure_cmake.sh > /dev/null
    BUILD_TARGETS="${HANNK_DIR}/build/compare_vs_tflite ${HANNK_DIR}/build/delegate/libHannkDelegate.so"
    cmake --build ${HANNK_DIR}/build -j`nproc` compare_vs_tflite
else
    echo Building [Make]...
    cd ${HANNK_DIR}
    BUILD_TARGETS="bin/${HL_TARGET}/compare_vs_tflite bin/${HL_TARGET}/libHannkDelegate.so"
    make -j `nproc` ${BUILD_TARGETS} > /dev/null
fi


# Remove and re-create $DEVICE_DIR, to avoid accidentally re-using stale blobs.
adb shell rm -rf "${DEVICE_DIR}"
adb shell mkdir -p "${DEVICE_DIR}"

adb push ${BUILD_TARGETS} ${TFLITE_SHARED_LIBRARY} ${LOCAL_FILES} ${DEVICE_DIR}/

adb shell LD_LIBRARY_PATH=${DEVICE_DIR}:${LD_LIBRARY_PATH} ${TASKSET_CMD} ${DEVICE_DIR}/compare_vs_tflite ${DEVICE_ARGS}

echo
echo All comparisons complete.
