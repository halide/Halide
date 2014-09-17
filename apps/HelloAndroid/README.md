HelloHalide is a simple application which applies a tone curve and
sharpening to a video preview from the camera on a phone or tablet.

This application builds for multiple native ABIs. (At present armeabi,
armeabi-v7a, arm64-v8a, mips, x86_64, and x86 are supported. mips64 is
not presently working.) Halide code is generated for each architecture.

This build is meant to use Android command line tools. (And IDE is not
required.) In order to build, the following will be required:

*Android NDK -- This can be download here:
    https://developer.android.com/tools/sdk/ndk/index.html
After installing, make sure the top-level directory of the install is
in the PATH. (It should contain an executable ndk-build file.)

*Android SDK -- This can be downloaded here:
    http://developer.android.com/sdk/index.html
The standalone SDK is desired. Once downloaded, the "android" program
in the tools directory of the install will need to be run. It should
bring up a UI allowing one to choose components to
install. HelloAndroid currently depends on the android-17 release. (It
can easily be made to run on others, but that is what the scripts are
setup to build against.) Make sure the tools directory is on one's
PATH.

*Apache Ant -- which can be downloaded here:
    http://ant.apache.org/bindownload.cgi
make sure the bin directory is on one's PATH.

If everything is setup correctly, running the build.sh script in this
directory, with the current directory set to here, whould build the
HelloAndroid apk and install it on a connected Android device.
