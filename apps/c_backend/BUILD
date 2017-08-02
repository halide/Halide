load(
    "@halide//:halide.bzl",
    "halide_generator",
    "halide_language_copts",
    "halide_language_linkopts",
    "halide_library_from_generator",
    "halide_runtime_linkopts",
)

halide_generator(
    name = "pipeline_generator",
    srcs = ["pipeline_generator.cpp"],
)

halide_library_from_generator(
    name = "pipeline_c",
    generator = ":pipeline_generator",
    includes = ["."],
)

halide_library_from_generator(
    name = "pipeline_native",
    generator = ":pipeline_generator",
    includes = ["."],
)

cc_test(
    name = "run",
    srcs = [
        "run.cpp",
        ":pipeline_c_cc",
        ":pipeline_c_h",
    ],
    linkopts = halide_runtime_linkopts(),
    deps = [
        ":pipeline_native",
    ],
)

halide_generator(
    name = "pipeline_cpp_generator",
    srcs = ["pipeline_cpp_generator.cpp"],
)

halide_library_from_generator(
    name = "pipeline_cpp_cpp",
    generator = ":pipeline_cpp_generator",
    halide_target_features = ["c_plus_plus_name_mangling"],
    includes = ["."],
)

halide_library_from_generator(
    name = "pipeline_cpp_native",
    generator = ":pipeline_cpp_generator",
    halide_target_features = ["c_plus_plus_name_mangling"],
    includes = ["."],
)

cc_test(
    name = "run_cpp",
    srcs = [
        "run_cpp.cpp",
        ":pipeline_cpp_cpp_cc",
        ":pipeline_cpp_cpp_h",
    ],
    linkopts = halide_runtime_linkopts(),
    deps = [
        ":pipeline_cpp_native",
    ],
)
