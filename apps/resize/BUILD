load("@halide//:halide.bzl", "halide_language_copts", "halide_language_linkopts")
load("@halide//apps/support:exec_test.bzl", "exec_test")

cc_binary(
    name = "resize",
    srcs = [
        "resize.cpp",
    ],
    copts = halide_language_copts(),
    linkopts = halide_language_linkopts(),
    deps = [
        "//tools:halide_benchmark",  # TODO add @halide when https://github.com/bazelbuild/bazel/issues/1248 is fixed
        "@halide//:language",
        "@halide//tools:halide_image_io",
    ],
)

exec_test(
    name = "test_resize",
    cmd = [
        "$(location :resize) $(location @halide//apps/images:rgba.png) out.png",
        "test -f out.png || exit",
    ],
    data = [
        ":resize",
        "@halide//apps/images:rgba.png",
    ],
)
