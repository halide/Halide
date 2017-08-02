load("@halide//:halide.bzl", "halide_language_copts", "halide_language_linkopts")

cc_test(
    name = "internal",
    srcs = ["internal.cpp"],
    copts = halide_language_copts(),
    linkopts = halide_language_linkopts(),
    deps = [
        "//:internal_test_includes",  # TODO add @halide when https://github.com/bazelbuild/bazel/issues/1248 is fixed
        "@halide//:language",
    ],
)
