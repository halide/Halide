#!/bin/bash

# This script will build and run this app on an Android device via
# adb. You must set the HL_TARGET environment variable prior to
# running this script, e.g.
#  HL_TARGET=arm-64-android adb_run_on_device.sh

DEVICE_PATH=/data/local/tmp/hexagon_benchmarks
DEVICE_ENV="LD_LIBRARY_PATH=${DEVICE_PATH}:/vendor/lib64 ADSP_LIBRARY_PATH=\"${DEVICE_PATH};/dsp\""
HEXAGON_RUNTIME_PATH=../../src/runtime/hexagon_remote
BIN=bin

#TODO: It would be nice to just use HL_TARGET, but that means one
#cannot enable extra target features without jumping through hoops. If
#we had a way to extra the 'base target' from an HL_TARGET environment
#variable...
APP_TARGET=arm-64-android

# Build the app.
make bin/${APP_TARGET}/process

# Make a folder on device for the app and our dependencies.
if [ $? == 0 ]
then
    adb shell mkdir -p ${DEVICE_PATH}

# Push the Hexagon runtime to $DEVICE_PATH.
    adb push ${HEXAGON_RUNTIME_PATH}/bin/${APP_TARGET}/libhalide_hexagon_host.so ${DEVICE_PATH}
    adb push ${HEXAGON_RUNTIME_PATH}/bin/v60/signed_by_debug/libhalide_hexagon_remote_skel.so ${DEVICE_PATH}

# If there's a testsig installed in the usual location, copy it to
# $DEVICE_PATH so it is visible to our modified $ASDP_LIBRARY_PATH.
    adb shell cp /system/lib/rfsa/adsp/testsig* ${DEVICE_PATH} > /dev/null || true

# Push and run the app!
    adb push ${BIN}/${APP_TARGET}/process ${DEVICE_PATH}
    adb shell chmod +x ${DEVICE_PATH}/process
    adb shell ${DEVICE_ENV} ${DEVICE_PATH}/process
fi
