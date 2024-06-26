HelloHalide is a simple application which applies a tone curve and sharpening to
a video preview from the camera on a phone or tablet.

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
  to install. HelloAndroid currently depends on the android-17 release. (It can
  easily be made to run on others, but that is what the scripts are setup to
  build against.) Make sure the tools directory is on one's PATH.

- Apache Ant -- which can be downloaded here:
  http://ant.apache.org/bindownload.cgi make sure the bin directory is on one's
  PATH.

If everything is setup correctly, running the build.sh script in this directory,
with the current directory set to here, whould build the HelloAndroid apk and
install it on a connected Android device.

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
you will be getting compilation errors complaining about a missing hello.h file:

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

# Android Studio

To load project into Android Studio use "File/Import Project..." in Android
Studio and point to apps/HelloAndroid/build.gradle file.

You will have to edit automatically-generated local.properties file to add
ndk.dir property so it points to your Android NDK installation as described in
Gradle section above.
