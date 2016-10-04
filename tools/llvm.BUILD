# Description:
#  Private BUILD file for LLVM use inside Halide.
#  Should not be used by code outside of Halide itself.

package(
    # TODO we should restrict the visibility here
    default_visibility = ["//visibility:public"],
)

load("//:llvm_internal_build_defs.bzl", "get_llvm_copts", "get_llvm_linkopts", "get_llvm_static_libs")

filegroup(
    name = "llvm-as",
    srcs = ["bin/llvm-as"],
)

filegroup(
    name = "clang",
    srcs = ["bin/clang"],
)

cc_library(
    name = "llvm",
    srcs = get_llvm_static_libs(),
    hdrs = glob([
        "include/llvm/**/*.def",
        "include/llvm/**/*.h",
        "include/llvm-c/**/*.h",
        "build_include/llvm/**/*.h",
        "build_include/llvm/**/*.def",
        "build_include/llvm/**/*.gen",
    ]),
    copts = get_llvm_copts(),
    includes = [
        "build_include",
        "include",
    ],
    linkopts = get_llvm_linkopts(),
    linkstatic = 1,
)

filegroup(
    name = "llvm_static_libs",
    srcs = get_llvm_static_libs(),
)
