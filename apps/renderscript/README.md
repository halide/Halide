Halide Renderscript "blur" and "copy" samples
===

These samples demonstrates two supported modes of operation for Renderscript codegen:
 - vectorized operations over interleaved RGBA images;
 - single-element operations over planar images.

Filters have to be built with '-user_context' option because currently
Halide Renderscript runtime expects cache directory for Renderscript artifacts
to be passed via user_context argument.

Prerequisites
---

You will need to have Android NDK in the path.
This app at the moment only runs on arm-based Android devices.
TODO(aam): Investigate and confirm that the app runs on x86 Android devices as well.

Building and running
---

1) ./build.sh script will build and deploy sample Java application to the
connected device. Results of the run are written to the device log, so scripts
ends with 'adb logcat', which will contains those results.

2) Invoking make will build and run C++ sample app with same tests as above,
but this will work only on rooted device where host machine can copy binary
files over to the device and run them via 'adb shell'.
