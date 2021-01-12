#!/bin/bash
#
# Run the compare_vs_tflite binary on device. This script will build and push the
# binary to the device, push the tflite file(s), the run the binary.
#
# export ANDROID_SERIAL as an env var to choose a specific device (if you have multiple connected via adb).
#
# export HL_TARGET to specify the target architecture to build. (Defaults to arm-64-android.)
#
# usage: HL_TARGET=arm-32-android run_device_on_target local_testdata/*.tflite

set -e

APP_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

export HL_TARGET=${HL_TARGET:-arm-64-android}

export TFLITE_SHARED_LIBRARY=${TFLITE_SHARED_LIBRARY:-${APP_DIR}/bin/tflite-android/jni/arm64-v8a/libtensorflowlite_jni.so}

BUILD_TARGETS="bin/${HL_TARGET}/compare_vs_tflite bin/${HL_TARGET}/libHalideDelegate.so"
DEVICE_DIR=/data/local/tmp/halide/compare_vs_tflite
BINARIES_TO_PUSH="${APP_DIR}/${BUILD_TARGET} ${APP_DIR}/${BUILD_TARGETS}"

if [[ -n "${ANDROID_SERIAL}" ]]; then
  echo Using ANDROID_SERIAL=${ANDROID_SERIAL}
fi
echo Using HL_TARGET=${HL_TARGET}

# Remove and re-create $DEVICE_DIR, to avoid accidentally re-using stale blobs.
adb shell rm -rf "${DEVICE_DIR}"
adb shell mkdir -p "${DEVICE_DIR}"

echo Building...
make -j `nproc` ${BUILD_TARGETS} > /dev/null

adb push ${BUILD_TARGETS} ${TFLITE_SHARED_LIBRARY} ${DEVICE_DIR}/ > /dev/null

if [ "$#" -eq 0 ]; then
    echo "Specify at least one .tflite file to use."
    exit 1
fi

adb push "$@" "${DEVICE_DIR}" > /dev/null

FILES=
for FILE in "$@"
do
  BASENAME=$(basename "${FILE}")
  FILES="${FILES} ${DEVICE_DIR}/${BASENAME}"
done

adb shell "LD_LIBRARY_PATH=${DEVICE_DIR}:${LD_LIBRARY_PATH} ${DEVICE_DIR}/compare_vs_tflite ${FILES}"

echo
echo All comparisons complete.
