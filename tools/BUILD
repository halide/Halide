cc_library(
    name = "halide_image_info",
    srcs = ["halide_image_info.h"],
    includes = ["."],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "halide_image_io",
    srcs = ["halide_image_io.h"],
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = ["@png_archive//:png"],
)

cc_library(
    name = "halide_malloc_trace",
    srcs = ["halide_malloc_trace.h"],
    includes = ["."],
    visibility = ["//visibility:public"],
)

cc_binary(
    name = "build_halide_h",
    srcs = ["build_halide_h.cpp"],
    visibility = ["//visibility:public"],
)

cc_binary(
    name = "bitcode2cpp",
    srcs = ["bitcode2cpp.cpp"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "gengen",
    srcs = [
        "GenGen.cpp",
    ],
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
