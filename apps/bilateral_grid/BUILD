load(
    "@halide//:halide.bzl",
    "halide_runtime_linkopts",
    "halide_generator",
    "halide_library",
)
load("@halide//apps/support:exec_test.bzl", "exec_test")

halide_generator(
    name = "bilateral_grid_generator",
    srcs = ["bilateral_grid_generator.cpp"],
)

halide_library(
    name = "bilateral_grid",
    generator = ":bilateral_grid_generator",
    includes = ["."],
)

cc_binary(
    name = "filter",
    srcs = ["filter.cpp"],
    linkopts = halide_runtime_linkopts(),
    deps = [
        "@halide//:runtime",
        "@halide//tools:halide_image_io",
        ":bilateral_grid",
        "//apps/support:benchmark",  # TODO add @halide when https://github.com/bazelbuild/bazel/issues/1248 is fixed
    ],
)

exec_test(
    name = "test_filter",
    cmd = [
        "$(location :filter) $(location @halide//apps/images:gray.png) out.png 0.1 10",
        "test -f out.png || exit",
    ],
    data = [
        "@halide//apps/images:gray.png",
        ":filter",
    ],
)

# TODO: wire up viz.sh to the relevant rules
