load(
    "@halide//:halide.bzl",
    "halide_runtime_linkopts",
    "halide_library",
)
load("@halide//apps/support:exec_test.bzl", "exec_test")

halide_library(
    name = "local_laplacian",
    srcs = ["local_laplacian_generator.cpp"],
    includes = ["."],
)

cc_binary(
    name = "process",
    srcs = ["process.cpp"],
    linkopts = halide_runtime_linkopts(),
    deps = [
        ":local_laplacian",
        "//tools:halide_benchmark",  # TODO add @halide when https://github.com/bazelbuild/bazel/issues/1248 is fixed
        "@halide//tools:halide_image_io",
    ],
)

exec_test(
    name = "test_process",
    cmd = [
        "$(location :process) $(location @halide//apps/images:rgb.png) 8 1 1 10 out.png",
        "test -f out.png || exit",
    ],
    data = [
        ":process",
        "@halide//apps/images:rgb.png",
    ],
)
