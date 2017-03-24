workspace(name = "halide")

load("//:halide_workspace.bzl", "halide_workspace")
halide_workspace()

# These assume $ANDROID_HOME and $ANDROID_NDK_HOME point to the SDK and NDK;
# if they aren't set, you can't build subpackages that rely on them
# (use --deleted_packages=apps/HelloAndroid to skip them)
#
# TODO: Bazel 0.4.5 doesn't properly ignore android_ndk_repository()
# when ANDROID_NDK_HOME is undefined (); use "--android_crosstool_top="
# to avoid this problem.

android_sdk_repository(name = "androidsdk")
android_ndk_repository(name = "androidndk")
