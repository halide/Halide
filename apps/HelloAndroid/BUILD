#
# To build for all common Android architectures:
#
# bazel build apps/HelloAndroid --fat_apk_cpu=armeabi-v7a,arm64-v8a,x86,x86_64
#

load(
    "@halide//:halide.bzl",
    "halide_runtime_linkopts",
    "halide_generator",
    "halide_library",
)

halide_generator(
    name = "hello_generator",
    srcs = ["jni/hello_generator.cpp"],
)

halide_library(
    name = "hello",
    generator = ":hello_generator",
    includes = ["."],
)

cc_library(
    name = "hello_wrapper",
    srcs = ["jni/hello_wrapper.cpp"],
    linkopts = [
        "-llog",
        "-landroid",
    ],
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
