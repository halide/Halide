load("@halide//:halide.bzl", "halide_library")

halide_library(
    name = "srgb_to_linear",
    srcs = ["srgb_to_linear_generator.cpp"],
)

halide_library(
    name = "linear_to_srgb",
    srcs = ["linear_to_srgb_generator.cpp"],
)

halide_library(
    name = "simple_blur",
    srcs = ["simple_blur_generator.cpp"],
    # For usage as a stub (via linear_blur), we don't need to specify
    # the type-and-dims explicitly, but for ahead-of-time compilation
    # (as used by run_linear_blur), the inputs must have
    # concrete type and dimensions. (The outputs must also have concrete
    # type-and-dimensions, too, but those can be inferred from the
    # outputs as filled in by user code.)
    generator_args = " ".join([
        "input.type=float32",
        "input.dim=3",
    ]),
)

halide_library(
    name = "linear_blur",
    srcs = ["linear_blur_generator.cpp"],
    # Emit the .stmt and .schedule files so we can
    # examine how the auto-scheduled results look
    extra_outputs = [
        "stmt",
        "schedule",
    ],
    generator_args = " ".join(["auto_schedule=true"]),
    generator_deps = [
        ":srgb_to_linear.generator",
        ":linear_to_srgb.generator",
        ":simple_blur.generator",
    ],
)

cc_binary(
    name = "run_linear_blur",
    srcs = ["run_linear_blur.cpp"],
    deps = [
        ":linear_blur",
        ":simple_blur",
        "@halide//:halide_buffer",
        "@halide//:halide_image_io",
    ],
)
