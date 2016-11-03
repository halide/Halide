load(
    "@halide//:halide.bzl",
    "halide_runtime_linkopts",
    "halide_generator",
    "halide_library",
)
load("@halide//apps/support:exec_test.bzl", "exec_test")

halide_generator(
    name = "local_laplacian_generator",
    srcs = ["local_laplacian_generator.cpp"],
)

halide_library(
    name = "local_laplacian",
    generator = ":local_laplacian_generator",
    includes = ["."],
)

cc_binary(
    name = "process",
    srcs = ["process.cpp"],
    linkopts = halide_runtime_linkopts(),
    deps = [
        "@halide//tools:halide_image_io",
        ":local_laplacian",
        "//apps/support:benchmark",  # TODO add @halide when https://github.com/bazelbuild/bazel/issues/1248 is fixed
    ],
)

exec_test(
    name = "test_process",
    cmd = [
        "$(location :process) $(location @halide//apps/images:rgb.png) 8 1 1 10 out.png",
        "test -f out.png || exit",
    ],
    data = [
        "@halide//apps/images:rgb.png",
        ":process",
    ],
)
