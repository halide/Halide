workspace(name = "halide")

load("//:halide_workspace.bzl", "halide_workspace")
halide_workspace()

# These assume $ANDROID_HOME and $ANDROID_NDK_HOME point to the SDK and NDK;
# if they aren't set, you can't build subpackages that rely on them
# (use --deleted_packages=apps/HelloAndroid to skip them)

android_sdk_repository(name = "androidsdk")
android_ndk_repository(name = "androidndk")

http_archive(
    name = "build_bazel_rules_apple",
    strip_prefix = "rules_apple-0.2.0",
    urls = ["https://github.com/bazelbuild/rules_apple/archive/0.2.0.tar.gz"],
)
