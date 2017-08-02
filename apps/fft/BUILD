load(
    "@halide//:halide.bzl",
    "halide_generator",
    "halide_language_copts",
    "halide_language_linkopts",
    "halide_library_from_generator",
    "halide_runtime_linkopts",
)
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
        "//tools:halide_benchmark",  # TODO add @halide when https://github.com/bazelbuild/bazel/issues/1248 is fixed
        "@halide//:language",
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
    copts = halide_language_copts(),
    deps = ["@halide//:language"],
)

halide_generator(
    name = "fft_generator",
    srcs = ["fft_generator.cpp"],
    deps = [":fft"],
)

halide_library_from_generator(
    name = "fft_forward_r2c",
    generator = ":fft_generator",
    generator_args = "direction=samples_to_frequency size0=16 size1=16 gain=0.00390625 input_number_type=real output_number_type=complex",
    includes = ["."],
)

halide_library_from_generator(
    name = "fft_inverse_c2r",
    generator = ":fft_generator",
    generator_args = "direction=frequency_to_samples size0=16 size1=16 input_number_type=complex output_number_type=real",
    includes = ["."],
)

halide_library_from_generator(
    name = "fft_forward_c2c",
    generator = ":fft_generator",
    generator_args = "direction=samples_to_frequency size0=16 size1=16 gain=0.00390625 input_number_type=complex output_number_type=complex",
    includes = ["."],
)

halide_library_from_generator(
    name = "fft_inverse_c2c",
    generator = ":fft_generator",
    generator_args = "direction=frequency_to_samples size0=16 size1=16 input_number_type=complex output_number_type=complex",
    includes = ["."],
)

cc_test(
    name = "fft_aot_test",
    srcs = ["fft_aot_test.cpp"],
    copts = ["-w"],
    linkopts = halide_runtime_linkopts(),
    deps = [
        ":fft_forward_c2c",
        ":fft_forward_r2c",
        ":fft_inverse_c2c",
        ":fft_inverse_c2r",
    ],
)
