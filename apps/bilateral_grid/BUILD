load(
    "@halide//:halide.bzl",
    "halide_library",
    "halide_runtime_linkopts",
)
load("@halide//apps/support:exec_test.bzl", "exec_test")

halide_library(
    name = "bilateral_grid",
    srcs = ["bilateral_grid_generator.cpp"],
    includes = ["."],
)

cc_binary(
    name = "filter",
    srcs = ["filter.cpp"],
    linkopts = halide_runtime_linkopts(),
    deps = [
        ":bilateral_grid",
        "//tools:halide_benchmark",  # TODO add @halide when https://github.com/bazelbuild/bazel/issues/1248 is fixed
        "@halide//:runtime",
        "@halide//tools:halide_image_io",
    ],
)

exec_test(
    name = "test_filter",
    cmd = [
        "$(location :filter) $(location @halide//apps/images:gray.png) out.png 0.1 10",
        "test -f out.png || exit",
    ],
    data = [
        ":filter",
        "@halide//apps/images:gray.png",
    ],
)

# TODO: wire up viz.sh to the relevant rules
