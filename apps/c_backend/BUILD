load("@halide//:halide.bzl", "halide_language_copts", "halide_language_linkopts", "halide_runtime_linkopts", "halide_library")

halide_library(
    name = "pipeline_c",
    srcs = ["pipeline_generator.cpp"],
    extra_outputs = ["cpp"],
    halide_target_map = {"*": ["*"]},
    includes = ["."],
)

halide_library(
    name = "pipeline_native",
    srcs = ["pipeline_generator.cpp"],
    includes = ["."],
)

cc_test(
    name = "run",
    srcs = ["run.cpp"],
    linkopts = halide_runtime_linkopts(),
    deps = [
        ":pipeline_c",
        ":pipeline_native",
    ],
)

halide_library(
    name = "pipeline_cpp_cpp",
    srcs = ["pipeline_cpp_generator.cpp"],
    extra_outputs = ["cpp"],
    halide_target_features = ["c_plus_plus_name_mangling"],
    halide_target_map = {"*": ["*"]},
    includes = ["."],
)

halide_library(
    name = "pipeline_cpp_native",
    srcs = ["pipeline_cpp_generator.cpp"],
    halide_target_features = ["c_plus_plus_name_mangling"],
    includes = ["."],
)

cc_test(
    name = "run_cpp",
    srcs = ["run_cpp.cpp"],
    linkopts = halide_runtime_linkopts(),
    deps = [
        ":pipeline_cpp_cpp",
        ":pipeline_cpp_native",
    ],
)
