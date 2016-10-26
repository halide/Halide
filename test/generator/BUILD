load("@halide//:halide.bzl", "halide_language_copts", "halide_language_linkopts", "halide_library", "halide_runtime_linkopts")

cc_library(
    name = "extended_buffer_t_common",
    srcs = ["extended_buffer_t_common.h"],
    includes = ["."],
)

# ----------------- Generators

# Generators that don't require any extra args, deps, etc
[halide_library(
    name = gen,
    srcs = ["%s_generator.cpp" % gen],
    includes = ["."],
) for gen in [
    "acquire_release",
    "argvcall",
    "can_use_target",
    "cleanup_on_error",
    "embed_image",
    "error_codes",
    "example",
    "float16_t",
    "gpu_object_lifetime",
    "gpu_only",
    "image_from_array",
    "mandelbrot",
    "memory_profiler_mandelbrot",
    "paramtest",
    "tiled_blur_blur",
    "tiled_blur",
    "variable_num_threads",
]]

# Generators that need extra args, etc
halide_library(
    name = "cxx_mangling",
    srcs = ["cxx_mangling_generator.cpp"],
    includes = ["."],
    namespace = "HalideTest",
)

halide_library(
    name = "cxx_mangling_define_extern",
    srcs = ["cxx_mangling_define_extern_generator.cpp"],
    filter_deps = [":cxx_mangling"],  # referenced via define_extern
    halide_target_features = ["user_context"],
    includes = ["."],
    namespace = "HalideTest",
)

halide_library(
    name = "extended_buffer_t",
    srcs = ["extended_buffer_t_generator.cpp"],
    generator_deps = [":extended_buffer_t_common"],
    includes = ["."],
)

halide_library(
    name = "matlab",
    srcs = ["matlab_generator.cpp"],
    halide_target_features = ["matlab"],
    includes = ["."],
)

_METADATA_TESTER_GENERATOR_ARGS = " ".join([
    "input.type=uint8",
    "input.dim=3",
    "output.type=float32,float32",
    "output.dim=3",
    "input_not_nod.type=uint8",
    "input_not_nod.dim=3",
    "input_nod.dim=3",
    "input_not.type=uint8",
    "array_input.size=2",
    "array_i8.size=2",
    "array_i16.size=2",
    "array_i32.size=2",
    "array_h.size=2",
    "array_outputs.size=2",
])

halide_library(
    name = "metadata_tester",
    srcs = ["metadata_tester_generator.cpp"],
    generator_args = _METADATA_TESTER_GENERATOR_ARGS,
    # TODO multitarget builds can subtly change some metadata output
    # (e.g. "output$3" instead of "output"); this should be fixed,
    # but as a workaround, disable multitarget via halide_target_map for now
    halide_target_map = {"*": ["*"]},
    includes = ["."],
)

halide_library(
    name = "metadata_tester_ucon",
    srcs = ["metadata_tester_generator.cpp"],
    generator_args = _METADATA_TESTER_GENERATOR_ARGS,
    halide_target_features = ["user_context"],
    # TODO multitarget builds can subtly change some metadata output
    # (e.g. "output$3" instead of "output"); this should be fixed,
    # but as a workaround, disable multitarget via halide_target_map for now
    halide_target_map = {"*": ["*"]},
    includes = ["."],
)

halide_library(
    name = "msan",
    srcs = ["msan_generator.cpp"],
    halide_target_features = ["msan"],
    includes = ["."],
)

halide_library(
    name = "multitarget",
    srcs = ["multitarget_generator.cpp"],
    halide_target_map = {"*": [
        "*-debug-c_plus_plus_name_mangling",
        "*-c_plus_plus_name_mangling",
    ]},
    includes = ["."],
    namespace = "HalideTest",
)

[halide_library(
    name = "nested_externs_%s" % gen_name,
    srcs = ["nested_externs_generator.cpp"],
    generator_name = "nested_externs_%s" % gen_name,
    includes = ["."],
) for gen_name in [
    "combine",
    "leaf",
]]

halide_library(
    name = "nested_externs_inner",
    srcs = ["nested_externs_generator.cpp"],
    filter_deps = [
        ":nested_externs_combine",
        ":nested_externs_leaf",
    ],
    generator_name = "nested_externs_inner",
    includes = ["."],
)

halide_library(
    name = "nested_externs_root",
    srcs = ["nested_externs_generator.cpp"],
    filter_deps = [
        ":nested_externs_combine",
        ":nested_externs_inner",
    ],
    generator_name = "nested_externs_root",
    includes = ["."],
)

halide_library(
    name = "pyramid",
    srcs = ["pyramid_generator.cpp"],
    generator_args = "levels=10",
    includes = ["."],
)

halide_library(
    name = "tiled_blur_interleaved",
    srcs = ["tiled_blur_generator.cpp"],
    generator_args = "is_interleaved=true",
    includes = ["."],
)

halide_library(
    name = "tiled_blur_blur_interleaved",
    srcs = ["tiled_blur_blur_generator.cpp"],
    generator_args = "is_interleaved=true",
    includes = ["."],
)

halide_library(
    name = "user_context",
    srcs = ["user_context_generator.cpp"],
    halide_target_features = ["user_context"],
    includes = ["."],
)

halide_library(
    name = "user_context_insanity",
    srcs = ["user_context_insanity_generator.cpp"],
    halide_target_features = ["user_context"],
    includes = ["."],
)

# ----------------- JIT Tests

# TODO the jit-tests #include .cpp files which confuses Bazel
# (and not unreasonably so); create header-only libraries to work around this
[cc_library(
    name = "%s_jittest_includes" % test,
    hdrs = [
        "%s_generator.cpp" % test,
    ],
) for test in [
    "example",
    "paramtest",
]]

[cc_test(
    name = "%s_jittest" % test,
    srcs = [
        "%s_jittest.cpp" % test,
    ],
    copts = halide_language_copts(),
    linkopts = halide_language_linkopts(),
    deps = [
        ":%s_jittest_includes" % test,
        "@halide//:language",
    ],
) for test in [
    "example",
    "paramtest",
]]

# ----------------- AOT Tests

# Simple AOT tests that predictably depend on a single halide_library of the same name
[cc_test(
    name = "%s_aottest" % test,
    srcs = ["%s_aottest.cpp" % test],
    linkopts = halide_runtime_linkopts(),
    deps = [
        ":%s" % test,
        "@halide//:language",
    ],
) for test in [
    "acquire_release",
    "argvcall",
    "can_use_target",
    "cxx_mangling",
    "cxx_mangling_define_extern",
    "embed_image",
    "error_codes",
    "example",
    "float16_t",
    "gpu_only",
    "image_from_array",
    "mandelbrot",
    "matlab",
    "memory_profiler_mandelbrot",
    "msan",
    "multitarget",
    "pyramid",
    "user_context",
    "user_context_insanity",
    "variable_num_threads",
]]

# AOT tests that have multiple deps, etc
cc_test(
    name = "cleanup_on_error_aottest",
    srcs = ["cleanup_on_error_aottest.cpp"],
    linkopts = halide_runtime_linkopts(),
    deps = [
        "@halide//:language",
        ":cleanup_on_error",
        "//:device_interface",  # TODO add @halide when https://github.com/bazelbuild/bazel/issues/1248 is fixed
    ],
)

cc_test(
    name = "metadata_tester_aottest",
    srcs = ["metadata_tester_aottest.cpp"],
    linkopts = halide_runtime_linkopts(),
    deps = [
        "@halide//:language",
        ":metadata_tester",
        ":metadata_tester_ucon",
    ],
)

cc_test(
    name = "extended_buffer_t_aottest",
    srcs = ["extended_buffer_t_aottest.cpp"],
    linkopts = halide_runtime_linkopts(),
    deps = [
        "@halide//:language",
        ":extended_buffer_t",
        ":extended_buffer_t_common",
    ],
)

cc_test(
    name = "gpu_object_lifetime_aottest",
    srcs = ["gpu_object_lifetime_aottest.cpp"],
    linkopts = halide_runtime_linkopts(),
    deps = [
        "@halide//:language",
        ":gpu_object_lifetime",
        "//test/common:gpu_object_lifetime_tracker",  # TODO add @halide when https://github.com/bazelbuild/bazel/issues/1248 is fixed
    ],
)

cc_test(
    name = "nested_externs_aottest",
    srcs = ["nested_externs_aottest.cpp"],
    linkopts = halide_runtime_linkopts(),
    deps = [
        "@halide//:language",
        ":nested_externs_root",
    ],
)

[cc_test(
    name = "tiled_blur%s_aottest" % suffix,
    srcs = ["tiled_blur%s_aottest.cpp" % suffix],
    linkopts = halide_runtime_linkopts(),
    deps = [
        ":tiled_blur%s" % suffix,
        ":tiled_blur_blur%s" % suffix,
        "@halide//:language",
    ],
) for suffix in [
    "",
    "_interleaved",
]]
