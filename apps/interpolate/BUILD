load(
    "@halide//:halide.bzl",
    "halide_language_copts",
    "halide_language_linkopts",
)
load("@halide//apps/support:exec_test.bzl", "exec_test")

cc_binary(
    name = "interpolate",
    srcs = [
        "interpolate.cpp",
    ],
    copts = halide_language_copts(),
    linkopts = halide_language_linkopts(),
    deps = [
        "@halide//:language",
        "@halide//tools:halide_image_io",
        "//apps/support:benchmark",  # TODO add @halide when https://github.com/bazelbuild/bazel/issues/1248 is fixed
    ],
)

exec_test(
    name = "test_interpolate",
    cmd = [
        "$(location :interpolate) $(location @halide//apps/images:rgba.png) out.png",
        "test -f out.png || exit",
    ],
    data = [
        "@halide//apps/images:rgba.png",
        ":interpolate",
    ],
)
