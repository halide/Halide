# Bazel build rules for clients using Halize distributions.
# Note that these rules aren't meant to build Halide itself;
# they assume that a Halide library has already been built,
# and that a downstream client wants to use it.
#
# These rules have only been tested with Bazel 0.6+, and are unlikely
# to work with earlier versions of Bazel.

package(
    default_visibility = ["//visibility:public"],
)

load(
    "@halide//:halide.bzl",
    "halide_config_settings",
    "halide_language_copts",
    "halide_library_runtimes",
)

halide_config_settings()

halide_library_runtimes()

cc_library(
    name = "language",
    hdrs = ["include/Halide.h"],
    copts = halide_language_copts(),
    includes = ["include"],
    deps = [
        ":lib_halide_static",
        ":runtime",
    ],
)

# You should rarely need to add an explicit dep on this library
# (the halide_library() rule will add it for you), but there are
# unusual circumstances where it is necessary.
cc_library(
    name = "runtime",
    hdrs = glob(["include/HalideRuntime*.h"]),
    includes = ["include"],
)

# Header-only library to let clients to use Halide::Buffer at runtime.
# (Generators should never need to use this library.)
cc_library(
    name = "halide_buffer",
    hdrs = glob(["include/HalideBuffer*.h"]),
    includes = ["include"],
)

# Config setting to catch the case where someone is trying to build
# on Windows, but forgot to specify --host_cpu=x64_windows_msvc AND
# --cpu=x64_windows_msvc.
config_setting(
    name = "windows_not_using_msvc",
    values = {"cpu": "x64_windows"},
)

cc_library(
    name = "lib_halide_static",
    srcs = select({
        ":windows_not_using_msvc": [
            "please_set_host_cpu_and_cpu_to_x86_64_windows",
        ],
        "@halide//:halide_config_x86_64_windows": [
            "Release/Halide.lib",
            "Release/Halide.dll",
        ],
        "//conditions:default": [
            "lib/libHalide.a",
        ],
    }),
    visibility = ["//visibility:private"],
)

cc_library(
    name = "halide_image_io",
    hdrs = ["tools/halide_image_io.h"],
    # TODO: include references to PNG and JPEG libraries;
    # for now, just disable these two
    defines = [
        "HALIDE_NO_JPEG",
        "HALIDE_NO_PNG",
    ],
    includes = [
        "include",
        "tools",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":runtime",
    ],
)

cc_library(
    name = "halide_benchmark",
    hdrs = ["tools/halide_benchmark.h"],
    includes = [
        "include",
        "tools",
    ],
)

# This library is visibility:public, because any package that uses the
# halide_library() rule will implicitly need access to it; that said, it is
# intended only for the private, internal use of the halide_library() rule.
# Please don't depend on it directly; doing so will likely break your code at
# some point in the future.
cc_library(
    name = "gengen",
    srcs = ["tools/GenGen.cpp"],
    includes = ["include"],
    deps = [":language"],
)

# This library is visibility:public, because any package that uses the
# halide_library() rule will implicitly need access to it; that said, it is
# intended only for the private, internal use of the halide_library() rule.
# Please don't depend on it directly; doing so will likely break your code at
# some point in the future.
cc_library(
    name = "rungen",
    srcs = [
        "tools/RunGen.cpp",
    ],
    includes = ["include"],
    deps = [
        ":halide_benchmark",
        ":halide_buffer",
        ":halide_image_io",
        ":runtime",
    ],
)

exports_files([
    "tools/RunGenStubs.cpp",
])
