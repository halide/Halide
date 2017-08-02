load(
    "@halide//:halide.bzl",
    "halide_language_copts",
)

cc_library(
    name = "halide_image_info",
    hdrs = ["halide_image_info.h"],
    includes = ["."],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "halide_image_io",
    hdrs = ["halide_image_io.h"],
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "@jpeg_archive//:jpeg",
        "@png_archive//:png",
    ],
)

cc_library(
    name = "halide_benchmark",
    hdrs = ["halide_benchmark.h"],
    includes = ["."],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "halide_malloc_trace",
    hdrs = ["halide_malloc_trace.h"],
    includes = ["."],
    visibility = ["//visibility:public"],
)

cc_binary(
    name = "build_halide_h",
    srcs = ["build_halide_h.cpp"],
    visibility = ["//visibility:public"],
)

cc_binary(
    name = "binary2cpp",
    srcs = ["binary2cpp.cpp"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "gengen",
    srcs = [
        "GenGen.cpp",
    ],
    copts = halide_language_copts(),
    visibility = ["//visibility:public"],
    deps = [
        "@halide//:language",
    ],
)

filegroup(
    name = "distrib_files",
    srcs = [
        "GenGen.cpp",
        "halide_image_info.h",
        "halide_image_io.h",
        "mex_halide.m",
    ],
    visibility = ["//:__subpackages__"],  # TODO add @halide when https://github.com/bazelbuild/bazel/issues/1248 is fixed
)

exports_files([
    "halide_distrib.BUILD",
    "static_link.sh",
])
