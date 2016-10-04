load("@halide//:halide.bzl", "halide_language_copts", "halide_language_linkopts")

[cc_test(
    name = test[0:-4],
    srcs = [test],
    copts = halide_language_copts(),
    linkopts = halide_language_linkopts(),
    deps = ["@halide//:language"] + [
        "//test/common:check_call_graphs",  # TODO add @halide when https://github.com/bazelbuild/bazel/issues/1248 is fixed
    ] + (["//test/common:gpu_object_lifetime_tracker"] if ("gpu_object_lifetime" in test) else []),
) for test in glob(["*.cpp"])]
