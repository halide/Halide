#!/bin/bash
#
# Run the compare_vs_tflite binary on device. This script will build and push the
# binary to the device, push the tflite file(s), the run the binary.
#
# export ANDROID_SERIAL as an env var to choose a specific device (if you have multiple connected via adb).
#
# export HL_TARGET to specify the target architecture to build. (Defaults to arm-64-android.)
#
# usage: HL_TARGET=arm-64-android ./run_compare_on_device.sh local_testdata/*.tflite --flag1 --flag2 ...

set -e

APP_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

export HL_TARGET=${HL_TARGET:-arm-64-android}

TENSORFLOW_BASE=${TENSORFLOW_BASE:-${HOME}/GitHub/tensorflow}
TFLITE_SHARED_LIBRARY=${TFLITE_SHARED_LIBRARY:-${TENSORFLOW_BASE}/tflite_build_android/libtensorflowlite_c.so}

BUILD_TARGETS="bin/${HL_TARGET}/compare_vs_tflite bin/${HL_TARGET}/libHannkDelegate.so"
DEVICE_DIR=/data/local/tmp/halide/compare_vs_tflite

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

LOCAL_FILES=
DEVICE_FILES=
FLAGS=
NEXT_IS_FLAG=0
for ARG in "$@"
do
  if [[ ${NEXT_IS_FLAG} -eq 1 ]]; then
    # assume it's a flag
    FLAGS="${FLAGS} ${ARG}"
    NEXT_IS_FLAG=0
  else
    if [[ "${ARG}" =~ ^-.* ]]; then
      # assume it's a flag
      FLAGS="${FLAGS} ${ARG}"
      NEXT_IS_FLAG=1
    else
      # assume it's a file
      BASENAME=$(basename "${ARG}")
      LOCAL_FILES="${LOCAL_FILES} ${ARG}"
      DEVICE_FILES="${DEVICE_FILES} ${DEVICE_DIR}/${BASENAME}"
    fi
  fi
done

adb push ${LOCAL_FILES} "${DEVICE_DIR}"
adb shell LD_LIBRARY_PATH=${DEVICE_DIR}:${LD_LIBRARY_PATH} ${DEVICE_DIR}/compare_vs_tflite ${FLAGS} ${DEVICE_FILES}

echo
echo All comparisons complete.
