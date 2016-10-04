load("@halide//:halide.bzl", "halide_config_settings")

halide_config_settings()

cc_library(
    name = "runtime",
    hdrs = glob([
        "include/HalideRuntime*.h",
        "include/HalideBuffer*.h",
    ]),
    includes = ["include"],
    visibility = ["//visibility:public"],
    alwayslink = 1,
)

cc_library(
    name = "language",
    srcs = ["lib/libHalide.a"],
    hdrs = glob(["include/*.h"]),
    includes = ["include"],
    visibility = ["//visibility:public"],
    alwayslink = 1,
)

# TODO: this requires use of install_name_tool to work usefully on OSX;
# eventually it would be nice to provide the downstream consumer the choice
# of binding the ":language" target to either the static library or the shared
# library, but for now, we'll just use the static one.
# cc_library(
#     name = "language",
#     hdrs = glob(["include/*.h"]),
#     includes = ["include"],
#     srcs = ["bin/libHalide.so"],
#     visibility = ["//visibility:public"],
# )

cc_library(
    name = "internal_halide_generator_glue",
    srcs = [
        "include/Halide.h",
        "tools/GenGen.cpp",
    ],
    linkstatic = 1,
    visibility = ["//visibility:public"],
    deps = [":language"],
)
