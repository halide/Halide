load(
    "@halide//:halide.bzl",
    "halide_language_copts",
    "halide_language_linkopts",
    "halide_generator",
    "halide_library_from_generator",
    "halide_runtime_linkopts",
)

# ----------------- halide_generator

# Simple cases
[halide_generator(
    name = "%s_generator" % gen[:-14],
    srcs = [gen],
    includes = ["."],
) for gen in glob(
    ["*_generator.cpp"],
    exclude = [
        "external_code*",  # TODO
        "nested_externs*",
        "stubtest*",
        "stubuser*",
    ],
)]

# Cases with extra deps and/or irregular generator_name
[halide_generator(
    name = "nested_externs_%s_generator" % gen_name,
    srcs = ["nested_externs_generator.cpp"],
    generator_name = "nested_externs_%s" % gen_name,
    includes = ["."],
) for gen_name in [
    "combine",
    "leaf",
    "inner",
    "root",
]]

halide_generator(
    name = "stubtest_generator",
    srcs = ["stubtest_generator.cpp"],
    generator_name = "StubNS1::StubNS2::StubTest",
    includes = ["."],
)

halide_generator(
    name = "stubuser_generator",
    srcs = ["stubuser_generator.cpp"],
    generator_name = "stubuser",
    includes = ["."],
    deps = [":stubtest_generator"],
)

# ----------------- halide_library_from_generator

# Libraries that don't require any generator args, features, etc
[halide_library_from_generator(
    name = gen,
    generator = ":%s_generator" % gen,
    includes = ["."],
) for gen in [
    "acquire_release",
    "argvcall",
    "blur2x2",
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
    "variable_num_threads",
]]

# Generators that need extra args, etc
cc_library(
    name = "cxx_mangling_externs",
    srcs = ["cxx_mangling_externs.cpp"]
)

halide_library_from_generator(
    name = "cxx_mangling",
    generator = ":cxx_mangling_generator",
    includes = ["."],
    namespace = "HalideTest::AnotherNamespace",
    deps = [":cxx_mangling_externs"],
)

halide_library_from_generator(
    name = "cxx_mangling_gpu",
    generator = ":cxx_mangling_generator",
    #halide_target_features = ["cuda"],     # TODO
    includes = ["."],
    namespace = "HalideTest",
    deps = [":cxx_mangling_externs"],
)

cc_library(
    name = "cxx_mangling_define_extern_externs",
    srcs = ["cxx_mangling_define_extern_externs.cpp"],
    deps = [":cxx_mangling"],
    linkstatic=1,  # TODO: necessary on OSX. Figure out why.
)

halide_library_from_generator(
    name = "cxx_mangling_define_extern",
    generator = ":cxx_mangling_define_extern_generator",
    halide_target_features = ["user_context"],
    includes = ["."],
    namespace = "HalideTest",
    deps = [
        ":cxx_mangling",  # referenced via define_extern
        ":cxx_mangling_define_extern_externs", 
    ],
)

halide_library_from_generator(
    name = "matlab",
    generator = ":matlab_generator",
    halide_target_features = ["matlab"],
    includes = ["."],
)

_METADATA_TESTER_GENERATOR_ARGS = " ".join([
    "input.type=uint8",
    "input.dim=3",
    "type_only_input_buffer.dim=3",
    "dim_only_input_buffer.type=uint8",
    "untyped_input_buffer.type=uint8",
    "untyped_input_buffer.dim=3",
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

halide_library_from_generator(
    name = "metadata_tester",
    generator = ":metadata_tester_generator",
    generator_args = _METADATA_TESTER_GENERATOR_ARGS,
    # TODO multitarget builds can subtly change some metadata output
    # (e.g. "output$3" instead of "output"); this should be fixed,
    # but as a workaround, disable multitarget via halide_target_map for now
    halide_target_map = {"*": ["*"]},
    includes = ["."],
)

halide_library_from_generator(
    name = "metadata_tester_ucon",
    generator = ":metadata_tester_generator",
    generator_args = _METADATA_TESTER_GENERATOR_ARGS,
    halide_target_features = ["user_context"],
    # TODO multitarget builds can subtly change some metadata output
    # (e.g. "output$3" instead of "output"); this should be fixed,
    # but as a workaround, disable multitarget via halide_target_map for now
    halide_target_map = {"*": ["*"]},
    includes = ["."],
)

halide_library_from_generator(
    name = "msan",
    generator = ":msan_generator",
    halide_target_features = ["msan"],
    includes = ["."],
)

halide_library_from_generator(
    name = "multitarget",
    generator = ":multitarget_generator",
    halide_target_map = {"*": [
        "*-debug-c_plus_plus_name_mangling",
        "*-c_plus_plus_name_mangling",
    ]},
    includes = ["."],
    namespace = "HalideTest",
)

[halide_library_from_generator(
    name = "nested_externs_%s" % gen_name,
    generator = ":nested_externs_%s_generator" % gen_name,
    includes = ["."],
) for gen_name in [
    "combine",
    "leaf",
]]

halide_library_from_generator(
    name = "nested_externs_inner",
    generator = ":nested_externs_inner_generator",
    includes = ["."],
    deps = [
        ":nested_externs_combine",
        ":nested_externs_leaf",
    ],
)

halide_library_from_generator(
    name = "nested_externs_root",
    generator = ":nested_externs_root_generator",
    includes = ["."],
    deps = [
        ":nested_externs_combine",
        ":nested_externs_inner",
    ],
)

halide_library_from_generator(
    name = "pyramid",
    generator = ":pyramid_generator",
    generator_args = "levels=10",
    includes = ["."],
)

halide_library_from_generator(
    name = "stubtest",
    generator = ":stubtest_generator",
    generator_args = " ".join([
        "untyped_buffer_input.type=uint8",
        "untyped_buffer_input.dim=3",
        "simple_input.type=float32",
        "array_input.type=float32",
        "array_input.size=2",
        "int_arg.size=2",
        "tuple_output.type=float32,float32",
    ]),
    includes = ["."],
)

halide_library_from_generator(
    name = "stubuser",
    generator = ":stubuser_generator",
    includes = ["."],
)

halide_library_from_generator(
    name = "tiled_blur",
    generator = ":tiled_blur_generator",
    includes = ["."],
    deps = [":blur2x2"],  # referenced via define_extern
)

halide_library_from_generator(
    name = "user_context",
    generator = ":user_context_generator",
    halide_target_features = ["user_context"],
    includes = ["."],
)

halide_library_from_generator(
    name = "user_context_insanity",
    generator = ":user_context_insanity_generator",
    halide_target_features = ["user_context"],
    includes = ["."],
)

# ----------------- JIT Tests

[cc_test(
    name = "%s_jittest" % test,
    srcs = ["%s_jittest.cpp" % test],
    copts = halide_language_copts() + ["-UNDEBUG"],
    linkopts = halide_language_linkopts(),
    deps = [
        ":%s_generator" % test,
        "@halide//:language",
    ],
) for test in [
    "example",
    "stubtest",
]]

# ----------------- AOT Tests

# Simple AOT tests that predictably depend on a single halide_library_from_generator of the same name
[cc_test(
    name = "%s_aottest" % test,
    srcs = ["%s_aottest.cpp" % test],
    copts = ["-UNDEBUG"],
    linkopts = halide_runtime_linkopts(),
    deps = [
        ":%s" % test,
    ],
) for test in [
    "acquire_release",
    "argvcall",
    "can_use_target",
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
    "stubtest",
    "stubuser",
    "tiled_blur",
    "user_context",
    "user_context_insanity",
    "variable_num_threads",
]]

# AOT tests that have multiple deps, etc
cc_test(
    name = "cleanup_on_error_aottest",
    srcs = ["cleanup_on_error_aottest.cpp"],
    copts = ["-UNDEBUG"],
    linkopts = halide_runtime_linkopts(),
    deps = [
        ":cleanup_on_error",
        "//:device_interface",  # TODO add @halide when https://github.com/bazelbuild/bazel/issues/1248 is fixed
    ],
)

cc_test(
    name = "cxx_mangling_aottest",
    srcs = ["cxx_mangling_aottest.cpp"],
    copts = ["-UNDEBUG"],
    linkopts = halide_runtime_linkopts(),
    deps = [
        ":cxx_mangling",
        ":cxx_mangling_gpu",
    ],
)

cc_test(
    name = "metadata_tester_aottest",
    srcs = ["metadata_tester_aottest.cpp"],
    copts = ["-UNDEBUG"],
    linkopts = halide_runtime_linkopts(),
    deps = [
        ":metadata_tester",
        ":metadata_tester_ucon",
    ],
)

cc_test(
    name = "gpu_object_lifetime_aottest",
    srcs = ["gpu_object_lifetime_aottest.cpp"],
    copts = ["-UNDEBUG"],
    linkopts = halide_runtime_linkopts(),
    deps = [
        ":gpu_object_lifetime",
        "//test/common:gpu_object_lifetime_tracker",  # TODO add @halide when https://github.com/bazelbuild/bazel/issues/1248 is fixed
    ],
)

cc_test(
    name = "nested_externs_aottest",
    srcs = ["nested_externs_aottest.cpp"],
    copts = ["-UNDEBUG"],
    linkopts = halide_runtime_linkopts(),
    deps = [
        ":nested_externs_root",
    ],
)
