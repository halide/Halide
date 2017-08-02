load(
    "@halide//:halide.bzl",
    "halide_language_copts",
    "halide_language_linkopts",
    "halide_library",
    "halide_runtime_linkopts",
)

cc_library(
    name = "daubechies_constants",
    hdrs = ["daubechies_constants.h"],
)

halide_library(
    name = "daubechies_x",
    srcs = ["daubechies_x_generator.cpp"],
    generator_deps = [":daubechies_constants"],
    includes = ["."],
)

halide_library(
    name = "haar_x",
    srcs = ["haar_x_generator.cpp"],
    generator_deps = [":daubechies_constants"],
    includes = ["."],
)

halide_library(
    name = "inverse_daubechies_x",
    srcs = ["inverse_daubechies_x_generator.cpp"],
    generator_deps = [":daubechies_constants"],
    includes = ["."],
)

halide_library(
    name = "inverse_haar_x",
    srcs = ["inverse_haar_x_generator.cpp"],
    generator_deps = [":daubechies_constants"],
    includes = ["."],
)

cc_binary(
    name = "wavelet",
    srcs = ["wavelet.cpp"],
    linkopts = halide_runtime_linkopts(),
    deps = [
        ":daubechies_x",
        ":haar_x",
        ":inverse_daubechies_x",
        ":inverse_haar_x",
        "@halide//tools:halide_image_io",
    ],
)

sh_test(
    name = "wavelet_test",
    srcs = ["wavelet_test.sh"],
    args = [
        "$(location :wavelet)",
        "$(location @halide//apps/images:gray.png)",
    ],
    data = [
        ":wavelet",
        "@halide//apps/images:gray.png",
    ],
)
