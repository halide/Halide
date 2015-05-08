This directory contains framework and build files for writing very simple apps to test, prototype, demo, or debug Halide features, especially on mobile platforms. You may create apps for iOS or OS X using a generator, test function, and a pair of CMake function calls. (A similar framework for Android will be added in the future.)

Very Simple Apps
================

The apps are very simple and consist of a small amount of boilerplate code automatically included in your project by the CMake functions. 

The basic app structure and behavior is the same across all platforms: one or more self-contained test functions, which can output text and images to a simple HTML view via a simple API (specified in SimpleAppAPI.h); additionally, the standard halide_print() and halide_error() functions in the Halide runtime are captured and displayed.

Although it is easy to add "tests" (really, any Halide-based experiments) directly to this project, the intended use case is really to provide a simple harness that can be duplicated and used for experimentation (especially device-specific testing and profiling) without requiring extensive knowledge of OS-specific app design and implementation.

Test Functions
--------------

The test function is a simple C function of the form `bool test(void)` that contains logic to initialize input, call the Halide generated function, and then verify the output. The test function may use extra runtime functions to display `buffer_t` contents or load buffer data from a URL.

    buffer_t g = {
    ...
    };
    halide_print(NULL,"Testing the CPU target\n");
    example(runtime_factor,&g);
    halide_buffer_display(&g);

You can log error messages to the app using `halide_error` and the return value from the test function will be printed by the app as well. Printed messages may contain HTML.

Building Apps
-------------

The iOS and OSX apps may be build using CMake and Xcode on OS X. You should start with a build of Halide based on the instructions in [README.md](../../README.md) in the Halide repository root directory. Build a static library version of libHalide.a.

To customize this harness to add your own tests or experiments:

  * Select a Halide::Generator derived class to include in the app. 
  * Write a test function and put it in a separate .cpp file. 
  * Edit the CMakeLists.txt in the ios/ and/or osx/ directories to add calls to `halide_add_*_generator_to_app()` function variants (for iOS, Mac OS X, etc.) to add the platform specific boilerplate code and Halide generated code for your app. 

To build and run, you then use CMake to build an XCode project. From the base directory of your Halide build, alongside the `build` directory you created following [README.md](../../README.md):

    mkdir build-ios
    cd build-ios
    cmake -GXcode ../test/platforms/ios -DHALIDE_INCLUDE_PATH=../include/ -DHALIDE_LIB_PATH=../bin/libHalide.a
    open test_ios.xcodeproj

NOTE: the iOS build is configured to build for arm64 only at this time; if you are building for an older 32-bit iOS device, change 

    target=ios-arm-64
    target=ios-arm-64-opengl

to

    target=ios-arm-32 
    target=ios-arm-32-opengl

in ios/CMakeLists.txt. (This will be updated to allow for 'fat' builds in the future.)
