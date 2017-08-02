load("@halide//:halide.bzl", "halide_language_copts", "halide_language_linkopts", "halide_opengl_linkopts")

[cc_test(
    name = test[0:-4],
    srcs = [
        test,
        "testing.h",
    ],
    copts = halide_language_copts() + [
        "-UNDEBUG",
        "-fexceptions",
    ],
    linkopts = halide_language_linkopts() + halide_opengl_linkopts(),
    deps = [
        "//:mini_opengl",  # TODO add @halide when https://github.com/bazelbuild/bazel/issues/1248 is fixed
        "@halide//:language",
    ],
) for test in glob(
    ["*.cpp"],
    exclude = ["internal.cpp"],
)]

cc_test(
    name = "internal",
    srcs = ["internal.cpp"],
    copts = halide_language_copts() + ["-UNDEBUG"],
    linkopts = halide_language_linkopts() + halide_opengl_linkopts(),
    deps = [
        "//:internal_test_includes",  # TODO add @halide when https://github.com/bazelbuild/bazel/issues/1248 is fixed
        "@halide//:language",
    ],
)
