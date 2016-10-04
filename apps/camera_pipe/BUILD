load("@halide//:halide.bzl", "halide_runtime_linkopts", "halide_library")
load("@halide//apps/support:exec_test.bzl", "exec_test")

halide_library(
    name = "camera_pipe",
    srcs = ["camera_pipe_generator.cpp"],
    includes = ["."],
)

cc_binary(
    name = "process",
    srcs = [
        "fcam/Demosaic.cpp",
        "fcam/Demosaic.h",
        "fcam/Demosaic_ARM.cpp",
        "fcam/Demosaic_ARM.h",
        "process.cpp",
    ],
    linkopts = halide_runtime_linkopts(),
    deps = [
        "@halide//tools:halide_image_io",
        "@halide//tools:halide_malloc_trace",
        ":camera_pipe",
        "//apps/support:benchmark",  # TODO add @halide when https://github.com/bazelbuild/bazel/issues/1248 is fixed
    ],
)

TIMING_ITERATIONS = 5

exec_test(
    name = "test_process",
    cmd = [
        "$(location :process) $(location @halide//apps/images:bayer_raw.png) 3700 2.0 50 %d out.png" % TIMING_ITERATIONS,
        "test -f out.png || exit",
    ],
    data = [
        "@halide//apps/images:bayer_raw.png",
        ":process",
    ],
)

# TODO: wire up viz.sh to the relevant rules
