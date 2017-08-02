load(
    "@halide//:halide.bzl",
    "halide_library",
    "halide_runtime_linkopts",
)

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
        ":halide_blur",
        "//tools:halide_benchmark",  # TODO add @halide when https://github.com/bazelbuild/bazel/issues/1248 is fixed
        "@halide//:runtime",
    ],
    # TODO: detect whether the C++ compiler supports this.
    # copts = ["-fopenmp"]
)
