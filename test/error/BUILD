load("@halide//:halide.bzl", "halide_language_copts", "halide_language_linkopts")
load("@halide//test/common:cc_failure_test.bzl", "cc_failure_test")

[cc_failure_test(
    name = "%s" % test[0:-4],
    srcs = [test],
    # TODO: test/error/wrong_type and test/error/float16_t_implicit_downcast
    # require that assert() always tests and fails, so we must undef NDEBUG
    # here. https://github.com/halide/Halide/issues/1565
    copts = halide_language_copts() + ["-UNDEBUG"],
    linkopts = halide_language_linkopts(),
    deps = ["@halide//:language"] + [
        # These are really only needed for a few tests, but easier (& harmless) to include in all of them
        "//test/common:halide_test_dirs",  # TODO add @halide when https://github.com/bazelbuild/bazel/issues/1248 is fixed
    ],
) for test in glob(["*.cpp"])]
