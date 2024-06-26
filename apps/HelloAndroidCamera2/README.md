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

This application builds for multiple native ABIs. (At present armeabi,
armeabi-v7a, arm64-v8a, x86_64, and x86 are supported.) Halide code is
generated for each architecture.

This build is meant to use Android command line tools. (An IDE is not required.)
In order to build, the following will be required:

- Android NDK -- This can be downloaded here:
  https://developer.android.com/tools/sdk/ndk/index.html After installing, make
  sure the top-level directory of the install is in the PATH. (It should contain
  an executable ndk-build file.)

- Android SDK -- This can be downloaded here:
  http://developer.android.com/sdk/index.html The standalone SDK is desired.
  Once downloaded, the "android" program in the tools directory of the install
  will need to be run. It should bring up a UI allowing one to choose components
  to install. HelloAndroidCamera2 currently depends on the android-21 release.
  Make sure the tools directory is on one's PATH.

- Apache Ant -- which can be downloaded here:
  http://ant.apache.org/bindownload.cgi make sure the bin directory is on one's
  PATH.

If everything is setup correctly, running the build.sh script in this directory,
with the current directory set to here, whould build the HelloAndroidCamera2 apk
and install it on a connected Android device.

# Gradle

To use Gradle create local.properties file in this folder with sdk.dir and
ndk.dir variables defined like so:

```
sdk.dir=/Users/joe/Downloads/android-sdk
ndk.dir=/Users/joe/Downloads/android-ndk
```

After that run `gradlew build` which will produce .apk file ready for deployment
to the Android device.

On Linux/Mac you can use `build-gradle.sh` to build, deploy and run this sample
application.

Pay attention to the list of platforms supported by your Halide installation.
They are listed in jni/Application.mk APP_ABI variable and in build.gradle archs
map. For example, if your Halide installation was built without arm64-v8a,
remove it from APP_ABI and archs. Both list and map should match, otherwise
you will be getting compilation errors complaining about a missing
halide_generated.h file:

```
:ndkBuild FAILED

FAILURE: Build failed with an exception.

* What went wrong:
Execution failed for task ':ndkBuild'.
...
  Output:
    /private/tmp/7/halide/apps/HelloAndroidCamera2/jni/native.cpp:11:26: fatal error: deinterleave.h: No such file or directory
     #include "deinterleave.h"

```

# Android Studio

To load project into Android Studio use "File/Import Project..." in Android
Studio and point to apps/HelloAndroidCamera2/build.gradle file.

You will have to edit automatically-generated local.properties file to add
ndk.dir property so it points to your Android NDK installation as described in
Gradle section above.
