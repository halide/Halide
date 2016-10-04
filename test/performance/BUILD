load("@halide//:halide.bzl", "halide_language_copts", "halide_language_linkopts")

cc_library(
    name = "benchmark",
    srcs = ["benchmark.h"],
)

[cc_test(
    name = test[0:-4],
    srcs = [test],
    copts = halide_language_copts(),
    linkopts = halide_language_linkopts(),
    deps = [
        "@halide//:language",
        ":benchmark",
    ],
) for test in glob(["*.cpp"])]
