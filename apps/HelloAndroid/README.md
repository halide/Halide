HelloHalide is a simple application which applies a tone curve and sharpening to
a video preview from the camera on a phone or tablet.

This app targets `arm64-v8a` and `x86_64`. The Halide pipeline
(`app/src/main/cpp/generators/hello_generator.cpp`) is compiled once per ABI via
Gradle's `externalNativeBuild`, which drives a single CMake project
(`app/src/main/cpp/CMakeLists.txt`); the Halide generator itself is built with
the host compiler as a side effect of that same CMake configure, so no separate
pre-build step is needed.

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
