#!/bin/bash

DEVICE_PATH=/data/local/tmp/blur
DEVICE_ENV="LD_LIBRARY_PATH=${DEVICE_PATH}:/vendor/lib64 ADSP_LIBRARY_PATH=\"${DEVICE_PATH};/dsp\""
HEXAGON_RUNTIME_PATH=../../src/runtime/hexagon_remote
BIN=bin

#TODO: It would be nice to just use HL_TARGET, but that means one
#cannot enable extra target features without jumping through hoops.
APP_TARGET=arm-64-android

make bin/${APP_TARGET}/test

adb shell mkdir -p ${DEVICE_PATH}

# Push the Hexagon runtime.
adb push ${HEXAGON_RUNTIME_PATH}/bin/${APP_TARGET}/libhalide_hexagon_host.so ${DEVICE_PATH}
adb push ${HEXAGON_RUNTIME_PATH}/bin/v60/signed_by_debug/libhalide_hexagon_remote_skel.so ${DEVICE_PATH}

# If there's a testsig installed, move it to $DEVICE_PATH.
adb shell cp /system/lib/rfsa/adsp/testsig* ${DEVICE_PATH} > /dev/null || true

# Push and run the app.
adb push ${BIN}/${APP_TARGET}/test ${DEVICE_PATH}
adb shell chmod +x ${DEVICE_PATH}/test
adb shell ${DEVICE_ENV} ${DEVICE_PATH}/test
