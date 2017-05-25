HelloHalide is a simple application which applies a tone curve and
sharpening to a video preview from the camera on a phone or tablet.

This application builds for multiple native ABIs. (At present armeabi,
armeabi-v7a, arm64-v8a, mips, x86_64, and x86 are supported. mips64 is
not presently working.) Halide code is generated for each architecture.

This app is built using Bazel 0.4.0 or later; Android Studio (v2.2 or later) is 
also supported via the "Android Studio with Bazel" plugin 
(https://plugins.jetbrains.com/plugin/9185).

To build, you'll need to ensure that you have the Android SDK and NDK installed.

*Android NDK -- This can be downloaded here:
    https://developer.android.com/tools/sdk/ndk/index.html
After installing, make sure the top-level directory of the install is
in the PATH. (It should contain an executable ndk-build file.)

*Android SDK -- This can be downloaded here:
    http://developer.android.com/sdk/index.html
The standalone SDK is desired. 

Alternately, if you have Android Studio installed, you can use the SDK/NDK 
provided as part of its install.

To use Gradle create local.properties file in this folder with sdk.dir and
ndk.dir variables defined like so:
```
sdk.dir=/Users/joe/Downloads/android-sdk
ndk.dir=/Users/joe/Downloads/android-ndk
```
After that run ```gradlew build``` which will produce .apk file ready for
deployment to the Android device.

On Linux/Mac you can use ```build-gradle.sh``` to build, deploy and run
this sample application.

Pay attention to the list of platforms supported by your Halide installation.
They are listed in jni/Application.mk APP_ABI variable
and in build.gradle archs map. For example, if your Halide installation was
built without mips support or without arm64-v8a, remove them from APP_ABI and
archs. Both list and map should match, otherwise you will be getting compilation
errors complaining about a missing hello.h file:

```
:compileDebugNdkClassic FAILED

FAILURE: Build failed with an exception.

* What went wrong:
Execution failed for task ':compileDebugNdkClassic'.
...
  Output:
    /private/tmp/7/halide/apps/HelloAndroid/jni/native.cpp:9:30: fatal error: hello.h: No such file or directory
     #include "hello.h"

```

Bazel
===
`bazel build apps/HelloAndroid` will build for armeabi-v7a (32-bit) by default.

To build for other architectures, use the --fat_apk_cpu flag to specify the 
cpu(s) for which you want Halide code included:

    bazel build apps/HelloAndroid --fat_apk_cpu=armeabi-v7a,arm64-v8a,x86,x86_64

Android Studio
===
After installing "Android Studio with Bazel", launch Android Stufio and select "Import Bazel Project". 

For workspace, select the path to your toplevel Halide install.

For project view, select "Generate from BUILD file" with the path apps/HelloAndroid/BUILD.

In the next screen enter

        directories:
          apps/HelloAndroid

        targets:
          //apps/HelloAndroid:HelloAndroid
          
        android_sdk_platform: android-17  # Or whatever API level you like

If you want to build for other than armeabi-v7a, select 
`Run > Edit Configurations`,  and specify --fat_apk_cpu as described earlier.
