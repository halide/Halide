load("@halide//:halide.bzl", "halide_language_copts", "halide_language_linkopts")

[cc_test(
    name = test[0:-4],
    srcs = [test],
    copts = halide_language_copts() +
            (["-fexceptions"] if ("exception" in test) else []),
    linkopts = halide_language_linkopts(),
    deps = ["@halide//:language"] + [
        "//test/common:check_call_graphs",  # TODO add @halide when https://github.com/bazelbuild/bazel/issues/1248 is fixed
        # Only needed for a few tests, but easier to include in all of them
        "//test/common:gpu_object_lifetime_tracker",  # TODO add @halide when https://github.com/bazelbuild/bazel/issues/1248 is fixed
    ],
) for test in glob(["*.cpp"])]

[cc_test(
    name = test[0:-2],
    srcs = [test],
    deps = ["@halide//:runtime"],
) for test in glob(["*.c"])]
