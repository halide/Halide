load("@halide//:halide.bzl", "halide_language_copts", "halide_language_linkopts")

[cc_test(
    name = test[0:-4],
    srcs = [test],
    copts = halide_language_copts() + [
        "-UNDEBUG",
    ] + (["-fexceptions"] if ("exception" in test) else []),
    linkopts = halide_language_linkopts(),
    deps = ["@halide//:language"] + [
        # These are really only needed for a few tests, but easier (& harmless) to include in all of them
        "//test/common:check_call_graphs",  # TODO add @halide when https://github.com/bazelbuild/bazel/issues/1248 is fixed
        "//test/common:gpu_object_lifetime_tracker",  # TODO add @halide when https://github.com/bazelbuild/bazel/issues/1248 is fixed
        "//test/common:halide_test_dirs",  # TODO add @halide when https://github.com/bazelbuild/bazel/issues/1248 is fixed
        "//tools:halide_image_io",  # TODO add @halide when https://github.com/bazelbuild/bazel/issues/1248 is fixed
    ],
) for test in glob(
    ["*.cpp"],
    exclude = ["halide_buffer.cpp"],
)]

# Note that this test must *not* link in either libHalide, or a Halide runtime;
# this test should be usable without either. (Note that this requires an extra
# linker directive on Darwin to ensure halide_weak_device_free() is in fact weak.)
cc_test(
    name = "halide_buffer",
    srcs = ["halide_buffer.cpp"],
    copts = halide_language_copts() + [
        "-UNDEBUG",
    ],
    linkopts = halide_language_linkopts(),
    deps = ["@halide//:runtime"],
)

[cc_test(
    name = test[0:-2],
    srcs = [test],
    deps = ["@halide//:runtime"],
) for test in glob(["*.c"])]
