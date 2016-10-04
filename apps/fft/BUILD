load("@halide//:halide.bzl", "halide_language_copts", "halide_language_linkopts", "halide_library", "halide_runtime_linkopts")
load("@halide//apps/support:exec_test.bzl", "exec_test")

# TODO: add WITH_FFTW support
cc_binary(
    name = "bench_fft",
    srcs = [
        "complex.h",
        "fft.cpp",
        "fft.h",
        "funct.h",
        "main.cpp",
    ],
    copts = halide_language_copts(),
    linkopts = halide_language_linkopts(),
    deps = [
        "@halide//:language",
        "//apps/support:benchmark",  # TODO add @halide when https://github.com/bazelbuild/bazel/issues/1248 is fixed
    ],
)

exec_test(
    name = "bench_16x16",
    cmd = [
        "$(location :bench_fft) 16 16 ./",
        "test -f c2c.html || exit",
        "test -f r2c.html || exit",
        "test -f c2r.html || exit",
    ],
    data = [
        ":bench_fft",
    ],
)

cc_library(
    name = "fft",
    srcs = [
        "fft.cpp",
    ],
    hdrs = [
        "complex.h",
        "fft.h",
        "funct.h",
    ],
    deps = ["@halide//:language"],
)

halide_library(
    name = "fft_forward_r2c",
    srcs = ["fft_generator.cpp"],
    generator_args = "direction=samples_to_frequency size0=16 size1=16 gain=0.00390625 input_number_type=real output_number_type=complex",
    generator_deps = [":fft"],
    includes = ["."],
)

halide_library(
    name = "fft_inverse_c2r",
    srcs = ["fft_generator.cpp"],
    generator_args = "direction=frequency_to_samples size0=16 size1=16 input_number_type=complex output_number_type=real",
    generator_deps = [":fft"],
    includes = ["."],
)

halide_library(
    name = "fft_forward_c2c",
    srcs = ["fft_generator.cpp"],
    generator_args = "direction=samples_to_frequency size0=16 size1=16 gain=0.00390625 input_number_type=complex output_number_type=complex",
    generator_deps = [":fft"],
    includes = ["."],
)

halide_library(
    name = "fft_inverse_c2c",
    srcs = ["fft_generator.cpp"],
    generator_args = "direction=frequency_to_samples size0=16 size1=16 input_number_type=complex output_number_type=complex",
    generator_deps = [":fft"],
    includes = ["."],
)

cc_test(
    name = "fft_aot_test",
    srcs = ["fft_aot_test.cpp"],
    linkopts = halide_runtime_linkopts(),
    deps = [
        ":fft_forward_c2c",
        ":fft_forward_r2c",
        ":fft_inverse_c2c",
        ":fft_inverse_c2r",
    ],
)
