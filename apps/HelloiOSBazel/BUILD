load(
    "@halide//:halide.bzl",
    "halide_runtime_linkopts",
    "halide_generator",
    "halide_library",
)

[halide_generator(
    name = "reaction_diffusion_2_%s_generator" % phase,
    srcs = ["reaction_diffusion_2_generator.cpp"],
    generator_name = "reaction_diffusion_2_%s" % phase,
) for phase in [
    "init",
    "update",
    "render",
]]

[halide_library(
    name = "reaction_diffusion_2_%s" % phase,
    generator = ":reaction_diffusion_2_%s_generator" % phase,
    includes = ["."],
) for phase in [
    "init",
    "update",
    "render",
]]

objc_library(
    name = "HelloiOS",
    srcs = glob(
        ["*.m"],
        exclude = ["main.m"],
    ),
    hdrs = glob(["*.h"]),
    deps = [
        ":reaction_diffusion_2_init",
        ":reaction_diffusion_2_render",
        ":reaction_diffusion_2_update",
        "//:runtime",  # TODO add @halide when https://github.com/bazelbuild/bazel/issues/1248 is fixed
    ],
)

objc_binary(
    name = "ios-app-binary",
    srcs = [
        "main.m",
    ],
    linkopts = halide_runtime_linkopts(),
    sdk_frameworks = [
        "CoreGraphics",
        "QuartzCore",
    ],
    deps = [
        ":HelloiOS",
    ],
)

ios_application(
    name = "ios-app",
    binary = ":ios-app-binary",
    infoplist = "HelloiOS-Info.plist",
)
