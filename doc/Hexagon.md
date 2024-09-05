# Halide for Hexagon HVX

Halide supports offloading work to Qualcomm Hexagon DSP on Qualcomm Snapdragon
845/710 devices or newer. The Hexagon DSP provides a set of 128 byte vector
instruction extensions - the Hexagon Vector eXtensions (HVX). HVX is well suited
for image processing, and Halide for Hexagon HVX will generate the appropriate
HVX vector instructions from a program authored in Halide.

Halide can be used to compile Hexagon object files directly, by using a target
such as `hexagon-32-qurt-hvx`.

Halide can also be used to offload parts of a pipeline to Hexagon using the
`hexagon` scheduling directive. To enable the `hexagon` scheduling directive,
include the `hvx` target feature in your target. The currently supported
combination of targets is to use the HVX target features with an x86 linux
host (to use the simulator) or with an ARM android target (to use Hexagon DSP
hardware). For examples of using the `hexagon` scheduling directive on both the
simulator and a Hexagon DSP, see the blur example app.

To build and run an example app using the Hexagon target,

1. Obtain and build trunk LLVM and Clang. (Earlier versions of LLVM may work but
   are not actively tested and thus not recommended.)
2. Download and install the Hexagon SDK and Hexagon Tools. Hexagon SDK 4.3.0 or
   later is needed. Hexagon Tools 8.4 or later is needed.
3. Build and run an example for Hexagon HVX

## 1. Obtain and build trunk LLVM and Clang

(Follow the instructions given previously, just be sure to check out the `main`
branch.)

## 2. Download and install the Hexagon SDK and Hexagon Tools

Go to https://qpm.qualcomm.com/#/main/home

1. Go to Tools, and download Qualcomm Package Manager 3. Install the package
   manager on your machine.
2. Run the installed Qualcomm Package Manager and install the Qualcomm Hexagon
   SDK 5.x (or 4.x). The SDK can be selected from the Qualcomm Hexagon SDK
   Products.
3. Set an environment variable to point to the SDK installation location
   ```
   export SDK_LOC=/location/of/SDK
   ```

## 3. Build and run an example for Hexagon HVX

In addition to running Hexagon code on device, Halide also supports running
Hexagon code on the simulator from the Hexagon tools.

To build and run the blur example in Halide/apps/blur on the simulator:

```
cd apps/blur
export HL_HEXAGON_SIM_REMOTE=../../src/runtime/hexagon_remote/bin/v65/hexagon_sim_remote
export HL_HEXAGON_TOOLS=$SDK_LOC/Hexagon_Tools/8.x/Tools/
LD_LIBRARY_PATH=../../src/runtime/hexagon_remote/bin/host/:$HL_HEXAGON_TOOLS/lib/iss/:. HL_TARGET=host-hvx make test
```

## To build and run the blur example in Halide/apps/blur on Android:

To build the example for Android, first ensure that you have Android NDK r19b or
later installed, and the ANDROID_NDK_ROOT environment variable points to it.
(Note that Qualcomm Hexagon SDK v4.3.0 includes Android NDK r19c, which is
fine.)

Now build and run the blur example using the script to run it on device:

```
export HL_HEXAGON_TOOLS=$SDK_LOC/HEXAGON_Tools/8.4.11/Tools/
HL_TARGET=arm-64-android-hvx ./adb_run_on_device.sh
```
