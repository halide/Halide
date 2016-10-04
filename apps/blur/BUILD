load("@halide//:halide.bzl", "halide_runtime_linkopts", "halide_library")

halide_library(
    name = "halide_blur",
    srcs = ["halide_blur_generator.cpp"],
    includes = ["."],
)

cc_test(
    name = "test",
    srcs = ["test.cpp"],
    copts = ["-Wno-unknown-pragmas"],
    linkopts = halide_runtime_linkopts(),
    deps = [
        "@halide//:runtime",
        ":halide_blur",
        "//apps/support:benchmark",  # TODO add @halide when https://github.com/bazelbuild/bazel/issues/1248 is fixed
    ],
    # TODO: detect whether the C++ compiler supports this.
    # copts = ["-fopenmp"]
)
