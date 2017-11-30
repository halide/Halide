# To test on a simulator:
#
#     bazel build :ios-app
#
# then, in XCode, open the project directory
#
#      bazel-bin/apps/HelloiOS/ios-app.xcodeproj
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
    "halide_generator",
    "halide_library_from_generator",
    "halide_runtime_linkopts",
)

load("@build_bazel_rules_apple//apple:ios.bzl", "ios_application")

[halide_generator(
    name = "reaction_diffusion_2_%s_generator" % phase,
    srcs = ["HelloiOS/reaction_diffusion_2_generator.cpp"],
    generator_name = "reaction_diffusion_2_%s" % phase,
) for phase in [
    "init",
    "update",
    "render",
]]

[halide_library_from_generator(
    name = "reaction_diffusion_2_%s" % phase,
    generator = ":reaction_diffusion_2_%s_generator" % phase,
    halide_target_features = ["user_context"],
    includes = ["."],
    tags = ["manual"],
) for phase in [
    "init",
    "update",
    "render",
]]

[halide_library_from_generator(
    name = "reaction_diffusion_2_metal_%s" % phase,
    generator = ":reaction_diffusion_2_%s_generator" % phase,
    halide_target_features = [
        "metal",
        "user_context",
    ],
    includes = ["."],
    tags = ["manual"],
) for phase in [
    "init",
    "update",
    "render",
]]

objc_library(
    name = "HelloiOS",
    srcs = glob(["HelloiOS/*.mm"]),
    hdrs = glob(["HelloiOS/*.h"]),
    copts = ["-std=c++11"],
    tags = ["manual"],
    deps = [
        ":reaction_diffusion_2_init",
        ":reaction_diffusion_2_metal_init",
        ":reaction_diffusion_2_metal_render",
        ":reaction_diffusion_2_metal_update",
        ":reaction_diffusion_2_render",
        ":reaction_diffusion_2_update",
        "//:runtime",  # TODO add @halide when https://github.com/bazelbuild/bazel/issues/1248 is fixed
    ],
)

# objc_binary(
#     name = "ios-app-binary",
#     srcs = [
#         "HelloiOS/main.mm",
#     ],
#     linkopts = halide_runtime_linkopts(),
#     sdk_frameworks = [
#         "CoreGraphics",
#         "Metal",
#         "QuartzCore",
#     ],
#     tags = ["manual"],
#     deps = [
#         ":HelloiOS",
#     ],
# )

ios_application(
    name = "ios-app",
    bundle_id = "org.halide-lang.apps.HelloiOS",
    families = [
        "iphone",
        "ipad",
    ],
    infoplists = ["HelloiOS/HelloiOS-Info.plist"],
    minimum_os_version = "8.0",
    deps = [
        ":HelloiOS",
    ],
    sdk_frameworks = [
        "CoreGraphics",
        "Foundation",
        "Metal",
        "QuartzCore",
        "UIKit",
    ]
)
