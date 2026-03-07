# HelloiOS

An interactive reaction-diffusion iOS demo powered by Halide.

## Quick Start

```bash
cd apps/HelloiOS
Halide_ROOT=/path/to/halide/install ./setup.sh
open HelloiOS.xcworkspace
```

Then select the **HelloiOS** scheme in Xcode, pick a destination, and build.

## How It Works

The project is split into two parts:

1. **Generators/** -- A CMake project that builds the Halide generator on the
   host and runs it to produce iOS-targeted static libraries and headers for
   each reaction-diffusion kernel. Kernels are generated for both `iphoneos`
   and `iphonesimulator` platforms, so you can switch destinations in Xcode
   without re-running setup.

2. **HelloiOS/** -- A native Xcode project for the iOS app. It compiles the
   Objective-C++ sources and links the generated Halide libraries. The app
   renders a reaction-diffusion simulation using Metal, with a CPU alternative
   available via double-tap.

Both live in **HelloiOS.xcworkspace**. A one-time `setup.sh` configures the
CMake side and writes a `GeneratorPaths.xcconfig` that the Xcode project reads
for paths. A Run Script build phase in Xcode invokes `rebuild-kernels.sh` so
generator changes are picked up automatically.

## Prerequisites

Halide must be built (or installed) first:

```bash
cd /path/to/Halide
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix install
```

## Environment Variables

| Variable      | Default      | Description           |
|---------------|--------------|-----------------------|
| `Halide_ROOT` | *(required)* | Halide install prefix |

## Launching in Simulator

```bash
open -a Simulator
xcrun simctl bootstatus "iPhone 16" -b
xcrun simctl install booted HelloiOS/build/Release-iphonesimulator/HelloiOS.app
xcrun simctl launch booted org.halide.HelloiOS
```

Or use Xcode's Run button after building for a simulator destination.

## Project Structure

```
HelloiOS/
  HelloiOS.xcworkspace/     Top-level workspace
  Generators/
    CMakeLists.txt           Builds generator + produces iOS kernels
    rebuild-kernels.sh       Called by Xcode Run Script build phase
    reaction_diffusion_2_generator.cpp
    build/                   Created by setup.sh
  HelloiOS/
    HelloiOS.xcodeproj/      Native Xcode project for the iOS app
    *.h / *.mm               App source files
  setup.sh                   One-time setup script
```
