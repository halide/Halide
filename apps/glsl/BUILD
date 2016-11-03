load(
    "@halide//:halide.bzl",
    "halide_runtime_linkopts",
    "halide_generator",
    "halide_library",
    "halide_opengl_linkopts",
)

halide_generator(
    name = "halide_blur_glsl_generator",
    srcs = ["halide_blur_glsl_generator.cpp"],
)

halide_library(
    name = "halide_blur_glsl",
    generator = ":halide_blur_glsl_generator",
    halide_target_features = [
        "opengl",
        "debug",
    ],
    includes = ["."],
)

halide_generator(
    name = "halide_ycc_glsl_generator",
    srcs = ["halide_ycc_glsl_generator.cpp"],
)

halide_library(
    name = "halide_ycc_glsl",
    generator = ":halide_ycc_glsl_generator",
    halide_target_features = [
        "opengl",
        "debug",
    ],
    includes = ["."],
)

cc_test(
    name = "opengl_test",
    srcs = [
        "opengl_test.cpp",
    ],
    linkopts = halide_runtime_linkopts() + halide_opengl_linkopts(),
    deps = [
        ":halide_blur_glsl",
        ":halide_ycc_glsl",
    ],
)
