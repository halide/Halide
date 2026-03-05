# HelloiOS - Halide iOS Demo App

An interactive reaction-diffusion simulation demonstrating Halide's ability to generate high-performance code for iOS, with dual-path rendering on both CPU and Metal GPU.

## What's Here

This directory contains **two build targets**:

1. **Host testing binary** (CMake) - Tests generators on macOS/Linux without requiring iOS
2. **iOS app** (Xcode) - Full iOS application for simulator or device

## Prerequisites

Build Halide first:

```bash
cd /path/to/Halide
cmake -G Ninja -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## 1. Host Testing (Optional)

Test that generators work on your host platform:

```bash
cd apps/HelloiOS

# Point CMake to Halide install tree (not build tree)
export CMAKE_PREFIX_PATH=/path/to/Halide/install

# Build and run test
cmake -G Ninja -S . -B test_build
cmake --build test_build
./test_build/test_reaction_diffusion_2
```

**Expected output:**
```
Testing HelloiOS reaction-diffusion generators...
  Testing reaction_diffusion_2_init...
    ✓ init passed
  Testing reaction_diffusion_2_update...
    ✓ update passed (10 iterations)
  Testing reaction_diffusion_2_render...
    ✓ render passed
✅ All HelloiOS generators passed!
```

**Note:** If CMake can't find Halide, you need the **install tree** (created by `cmake --install`), not the build tree. See troubleshooting below.

## 2. iOS App

### Option A: Build in Xcode (Recommended)

```bash
cd apps/HelloiOS
open HelloiOS.xcodeproj
```

Then in Xcode:
1. Select a simulator (e.g., "iPhone 16") or connected device
2. Press **⌘ + R** to build and run

### Option B: Command-Line Build

**For simulator:**

```bash
cd apps/HelloiOS
xcodebuild \
    -project HelloiOS.xcodeproj \
    -scheme HelloiOS \
    -sdk iphonesimulator \
    -configuration Release \
    build \
    CODE_SIGN_IDENTITY="" \
    CODE_SIGNING_REQUIRED=NO
```

**For device:**

```bash
cd apps/HelloiOS
xcodebuild \
    -project HelloiOS.xcodeproj \
    -scheme HelloiOS \
    -sdk iphoneos \
    -configuration Release \
    build
```

(Device builds require code signing configuration in Xcode first)

## How It Works

The app uses three Halide generators:

- **reaction_diffusion_2_init** - Initialize simulation state
- **reaction_diffusion_2_update** - Update simulation each frame
- **reaction_diffusion_2_render** - Render state to pixels

Each generator is compiled **twice** during the Xcode build:
- Once for **CPU** (ARM NEON vectorization) with target `arm-64-ios` or `arm-64-osx` (simulator)
- Once for **Metal GPU** with target `arm-64-ios-metal` or `arm-64-osx-metal`

The app switches between CPU and Metal at runtime (double-tap to toggle).

## Using the App

- **Tap** - Add white circle that disturbs the pattern
- **Double-tap** - Toggle between CPU and Metal rendering
- **Status bar** - Shows rendering mode and frame time (e.g., "16.7ms Metal")

Expected performance:
- **Metal mode:** ~5-15ms per frame (GPU)
- **CPU mode:** ~10-30ms per frame (NEON)

## Requirements

- **Host testing:** CMake 3.28+, C++17 compiler, Halide install tree
- **iOS app:** macOS with Xcode 16.0+, iOS 13.0+ deployment target, Halide build tree

## Troubleshooting

### "Could not find libHalide.dylib" (Xcode build)

The Xcode build script looks for Halide in:
- `../../bin/libHalide.dylib`
- `../../build/lib/libHalide.dylib`

**Solution:** Build Halide first (see Prerequisites above).

### "Could not find Halide" (CMake host test)

CMake needs Halide's **install tree**, not build tree.

**Solution:**

```bash
# Create install tree
cd /path/to/Halide
cmake --install build --prefix install

# Point CMake to it
export CMAKE_PREFIX_PATH=/path/to/Halide/install
cd apps/HelloiOS
cmake -G Ninja -S . -B test_build
```

**Why?** Build trees lack helper files like `HalideConfig.cmake`. Install trees are complete.

### Code signing required (device builds)

1. Open `HelloiOS.xcodeproj`
2. Select "HelloiOS" target
3. Go to "Signing & Capabilities"
4. Select your development team
5. Xcode will automatically provision the app

### Generator build fails in Xcode

**Check:**
- `../../tools/GenGen.cpp` exists
- `../../include/Halide.h` exists
- Halide built successfully

The generator builds on the host (macOS), not the iOS target. The Xcode build script handles this automatically.

### Metal shader errors

First run may show warnings as Metal compiles shaders. If errors persist:
- Clean build: Product → Clean Build Folder (⇧⌘K) in Xcode
- Check Console: View → Debug Area → Console
- Verify deployment target is iOS 13.0+

## Architecture Notes

- **Xcode project:** Hand-crafted, contains embedded build script that compiles generators
- **CMake:** Only for host testing, does NOT build iOS app
- **Why separate?** Cross-compiling generators is complex; Xcode handles it natively

## Technical Details

- **Reaction-diffusion:** Gray-Scott model with diffusion via 5-tap blur and coupled differential equations
- **Scheduling:** CPU uses parallelization + NEON vectorization; GPU uses tiled blocks
- **Metal:** iOS 13+ enables Metal on both devices and simulators (including Apple Silicon Macs)
- **Targets:** `arm-64-ios` for device, `arm-64-osx` or `x86-64-osx` for simulator

## Files

- `HelloiOS.xcodeproj` - Xcode project for iOS app
- `CMakeLists.txt` - Host testing only (rejects iOS builds)
- `test_reaction_diffusion_2.cpp` - Host test executable
- `HelloiOS/reaction_diffusion_2_generator.cpp` - Halide generators (3 pipelines)
- `HelloiOS/HalideView.mm` - Custom UIView with Metal/CoreGraphics rendering
- `HelloiOS/HalideViewController.mm` - Touch input handling
- `HelloiOS/AppDelegate.mm` - iOS app lifecycle

## License

MIT License (same as Halide). See `/LICENSE.txt` in repository root.
