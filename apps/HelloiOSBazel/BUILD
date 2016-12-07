# To test on a simulator:
#
#     bazel build :ios-app
#
# then, in XCode, open the project directory
#
#      bazel-bin/apps/HelloiOSBazel/ios-app.xcodeproj
#
# and choose an iOS Simulator as the runtime scheme and click the Run button.
#
# To build for an actual device:
#
#     bazel build --ios_multi_cpus=armv7,arm64 --ios_minimum_os=8.0 :ios-app
#
# The easiest way to install the app on the device is to launch Xcode and use
# the Windows > Devices command. Select your plugged in device from the list on
# the left, and then add the app by clicking on the "plus" sign under installed
# apps and selecting the .ipa that you built.

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

[halide_library(
    name = "reaction_diffusion_2_metal_%s" % phase,
    generator = ":reaction_diffusion_2_%s_generator" % phase,
    halide_target_features = ["metal", "user_context"],
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
        ":reaction_diffusion_2_metal_init",
        ":reaction_diffusion_2_metal_render",
        ":reaction_diffusion_2_metal_update",
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
        "Metal",
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
