#
# To build for all common Android architectures:
#
# bazel build apps/HelloAndroid --fat_apk_cpu=armeabi-v7a,arm64-v8a,x86,x86_64
#

load(
    "@halide//:halide.bzl",
    "halide_library",
    "halide_runtime_linkopts",
)

halide_library(
    name = "hello",
    srcs = ["jni/hello_generator.cpp"],
    includes = ["."],
)

cc_library(
    name = "hello_wrapper",
    srcs = ["jni/hello_wrapper.cpp"],
    linkopts = halide_runtime_linkopts(),
    tags = ["manual"],  # Don't compile this for ":all" targets
    deps = [":hello"],
)

# TODO: doesn't work yet, code expects libnative.so but sees libHelloAndroid.so
android_binary(
    name = "HelloAndroid",
    srcs = glob(["src/com/example/hellohalide/*.java"]),
    custom_package = "com.example.hellohalide",
    manifest = "AndroidManifest.xml",
    resource_files = glob(["res/**"]),
    deps = [
        ":hello_wrapper",
    ],
)
