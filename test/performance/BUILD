load("@halide//:halide.bzl", "halide_language_copts", "halide_language_linkopts")

[cc_test(
    name = test[0:-4],
    srcs = [test],
    copts = halide_language_copts() + ["-UNDEBUG"],
    linkopts = halide_language_linkopts(),
    deps = [
        "//tools:halide_benchmark",  # TODO add @halide when https://github.com/bazelbuild/bazel/issues/1248 is fixed
        "@halide//:language",
    ],
) for test in glob(["*.cpp"])]
