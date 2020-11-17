#!/bin/bash
#
# Run the benchmark binary on device. This script will build and push the
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

BENCHMARK_BINARY_NAME=benchmark
BENCHMARK_TARGET="bin/${HL_TARGET}/${BENCHMARK_BINARY_NAME}"
BENCHMARK_BINARY="${APP_DIR}/${BENCHMARK_TARGET}"
DEVICE_DIR="/data/local/tmp/halide/benchmarking"

if [[ -n "${ANDROID_SERIAL}" ]]; then
  echo Using ANDROID_SERIAL=${ANDROID_SERIAL}
fi
echo Using HL_TARGET=${HL_TARGET}

# Remove and re-create $DEVICE_DIR, to avoid accidentally re-using stale blobs.
adb shell rm -rf "${DEVICE_DIR}"
adb shell mkdir -p "${DEVICE_DIR}"

# Build and push the microbenchmark.
echo Building...
make -j `nproc` ${BENCHMARK_TARGET} > /dev/null

adb push "${BENCHMARK_BINARY}" "${DEVICE_DIR}/${BENCHMARK_BINARY_NAME}" > /dev/null

adb push "$@" "${DEVICE_DIR}" > /dev/null

FILES=
for FILE in "$@"
do
  BASENAME=$(basename "${FILE}")
  FILES="${FILES} ${DEVICE_DIR}/${BASENAME}"
done

adb shell "${DEVICE_DIR}/${BENCHMARK_BINARY_NAME} ${FILES}"

echo
echo All benchmarks complete.
