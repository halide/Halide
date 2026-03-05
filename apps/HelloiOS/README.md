# HelloiOS

An interactive reaction-diffusion iOS demo that builds its Halide generators on
the host, then builds the iOS app and generated Halide libraries from CMake.

## Supported Workflow

Use [`build.sh`](build.sh) with an iOS Halide target, for example:

```bash
cd apps/HelloiOS
./build.sh arm-64-ios-simulator
./build.sh arm-64-ios
```

The script does two builds:

1. A host build in `build/host` that exports `HelloiOS-halide_generators`
2. An iOS build in `build/<Halide_TARGET>` that imports that package, generates
   the Halide pipelines, and builds `HelloiOS.app`

For the iOS build, `build.sh` uses CMake's iOS cross-compilation support with:

- `-DCMAKE_SYSTEM_NAME=iOS`
- `-DCMAKE_OSX_SYSROOT=iphonesimulator` when `Halide_TARGET` contains
  `simulator`, otherwise `iphoneos`
- `-DCMAKE_OSX_ARCHITECTURES=arm64`
- `-DHalide_TARGET=<your argument>`

## Prerequisites

Halide must be installed first, and `Halide_ROOT` must be set to the Halide
install prefix before invoking `build.sh`.

```bash
cd /path/to/Halide
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix install

export Halide_ROOT=$PWD/install
```

`build.sh` sets `HalideHelpers_ROOT` from `Halide_ROOT` automatically.

## Useful Environment Variables

- `Halide_ROOT`: Halide install prefix. Required.
- `CMAKE_BUILD_TYPE`: Build configuration. Default: `Release`
- `CMAKE_OSX_DEPLOYMENT_TARGET`: iOS deployment target. Default: `15.0`
- `HELLOIOS_DEVELOPMENT_TEAM`: Optional Apple development team ID. If unset,
  code signing is disabled for the generated app target.
- `HOST_BUILD_DIR`: Override the host build directory
- `IOS_BUILD_DIR`: Override the iOS build directory

Example:

```bash
Halide_ROOT=$HOME/halide-install \
HELLOIOS_DEVELOPMENT_TEAM=ABCDE12345 \
./build.sh arm-64-ios
```

## Manual Equivalent

If you want to drive the two builds yourself, this is the structure `build.sh`
uses.

Host generator build:

```bash
cmake -S . -B build/host \
    -DHalide_ROOT=/path/to/install \
    -DHalideHelpers_ROOT=/path/to/install
cmake --build build/host --target HelloiOS-halide_generators
```

iOS app build:

```bash
cmake -G Xcode -S . -B build/arm-64-ios-simulator \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_SYSROOT=iphonesimulator \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
    -DHalide_ROOT=/path/to/install \
    -DHalideHelpers_ROOT=/path/to/install \
    -DHalide_TARGET=arm-64-ios-simulator \
    -DHelloiOS-halide_generators_ROOT=$PWD/build/host
cmake --build build/arm-64-ios-simulator --config Release --target HelloiOS
```

## Launching In Simulator

After building a simulator target, you can boot a device, install the app, and
launch it from the command line:

```bash
open -a Simulator
xcrun simctl bootstatus "iPhone 16" -b
xcrun simctl install booted build/arm-64-ios-simulator/Release-iphonesimulator/HelloiOS.app
xcrun simctl launch booted org.halide.HelloiOS
```

To see the available simulator device names:

```bash
xcrun simctl list devices available
```

## Notes

- [`CMakeLists.txt`](CMakeLists.txt) is the only supported CMake entry point.
- In a non-iOS configure, `CMakeLists.txt` only exports the host generator
  package.
- In an iOS configure, the same file builds the generated Halide libraries and
  the iOS app bundle.
- CMake generates the Xcode project in `build/<Halide_TARGET>`.
