load(
    "@halide//:halide.bzl",
    "halide_language_copts",
    "halide_language_linkopts",
    "halide_generator",
    "halide_library",
    "halide_runtime_linkopts",
)

cc_library(
    name = "daubechies_constants",
    hdrs = ["daubechies_constants.h"],
)

halide_generator(
    name = "daubechies_x_generator",
    srcs = ["daubechies_x_generator.cpp"],
    deps = [":daubechies_constants"],
)

halide_library(
    name = "daubechies_x",
    generator = ":daubechies_x_generator",
    includes = ["."],
)

halide_generator(
    name = "haar_x_generator",
    srcs = ["haar_x_generator.cpp"],
    deps = [":daubechies_constants"],
)

halide_library(
    name = "haar_x",
    generator = ":haar_x_generator",
    includes = ["."],
)

halide_generator(
    name = "inverse_daubechies_x_generator",
    srcs = ["inverse_daubechies_x_generator.cpp"],
    deps = [":daubechies_constants"],
)

halide_library(
    name = "inverse_daubechies_x",
    generator = ":inverse_daubechies_x_generator",
    includes = ["."],
)

halide_generator(
    name = "inverse_haar_x_generator",
    srcs = ["inverse_haar_x_generator.cpp"],
    deps = [":daubechies_constants"],
)

halide_library(
    name = "inverse_haar_x",
    generator = ":inverse_haar_x_generator",
    includes = ["."],
)

cc_binary(
    name = "wavelet",
    srcs = ["wavelet.cpp"],
    linkopts = halide_runtime_linkopts(),
    deps = [
        "@halide//tools:halide_image_io",
        ":daubechies_x",
        ":haar_x",
        ":inverse_daubechies_x",
        ":inverse_haar_x",
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
        "@halide//apps/images:gray.png",
        ":wavelet",
    ],
)
