cc_library(
    name = "benchmark",
    srcs = ["benchmark.h"],
    includes = ["."],
    visibility = ["//apps:__subpackages__"],  # TODO add @halide when https://github.com/bazelbuild/bazel/issues/1248 is fixed
)

exports_files(glob(["exec_test.sh"]))
