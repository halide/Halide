HelloAndroidCamera2 is a simple application which uses Halide to process images
streamed from the Android camera2 API. It reads every frame into the CPU via an
ImageReader and uses Halide to either blit the frame to the output surface
(converting between YUV formats), or apply an edge detector on the luma channel.
This example requires a phone or tablet that supports the camera2 API (Android
API level 21 or above). This sample has been tested on Nexus 5, Nexus 6 and
Nexus 9.

CAVEAT: This example uses the not-so-well-documented ANativeWindow C API to
directly write into the graphics buffers that support the Java "Surface" and
"SurfaceView" classes. In particular, we rely on the YV12 format and use the
ANativeWindow API to "reconfigure" buffers so that they do not have to match the
resolution of the display. This exploits the hardware scaler to resample the
displayed image. However, although camera2 reports a set of supported
resolutions for ImageReader, there is no such enumeration for the display. On
untested devices, chooseOptimalSize() may return camera resolution for which
there is no matching graphics resolution. This will lead to a green screen with
a logcat error message that looks something like:

E/halide_native( 6146): ANativeWindow buffer locked but its size was 1920 x
1440, expected 1440 x 1080

This app targets `arm64-v8a` and `x86_64`. The two Halide pipelines
(`app/src/main/cpp/generators/deinterleave_generator.cpp`,
`edge_detect_generator.cpp`) are compiled once per ABI via Gradle's
`externalNativeBuild`, which drives a single CMake project
(`app/src/main/cpp/CMakeLists.txt`); the Halide generators themselves are built
with the host compiler as a side effect of that same CMake configure, so no
separate pre-build step is needed.

## Prerequisites

- A local Halide install (built via CMake -- see the top-level
  `doc/BuildingHalideWithCMake.md`).
- Android Studio, or the Android SDK + NDK (Android Studio's SDK Manager can
  install the NDK under SDK Tools -- "NDK (Side by side)").
- A CMake >= 3.28. The CMake bundled with the Android SDK by default is often
  older than this; if so, point Gradle at a newer one by adding
  `cmake.dir=<path to a CMake >= 3.28 install prefix>` to `local.properties`
  (e.g. `cmake.dir=/opt/homebrew` for a Homebrew-installed CMake on macOS).

## Building

```
./gradlew assembleDebug -PHalide_ROOT=<path/to/halide install>
```

(`Halide_ROOT` can also be provided via an environment variable of the same
name.) This produces `app/build/outputs/apk/debug/app-debug.apk`, installable
with `adb install`.

To build in Android Studio instead, open this directory as a project, add
`Halide_ROOT=<path/to/halide install>` to `gradle.properties` (or
`local.properties`), and use the Run button.
