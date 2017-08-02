load(
    "@halide//:halide.bzl",
    "halide_generator",
    "halide_language_copts",
    "halide_language_linkopts",
    "halide_library_from_generator",
    "halide_runtime_linkopts",
)
load(":exec_test.bzl", "exec_test")

cc_library(
    name = "clock",
    hdrs = ["clock.h"],
)

# Several tutorials have unused variables, which is fine; silence the warnings.
_IGNORE_UNUSED = ["-Wno-unused-variable"]

# Many of the tutorials have trivial rules
[cc_binary(
    name = t,
    srcs = ["%s.cpp" % t],
    copts = halide_language_copts() + _IGNORE_UNUSED,
    linkopts = halide_language_linkopts(),
    deps = ["@halide//:language"],
) for t in [
    "lesson_01_basics",
    "lesson_03_debugging_1",
    "lesson_04_debugging_2",
    "lesson_05_scheduling_1",
    "lesson_06_realizing_over_shifted_domains",
    "lesson_08_scheduling_2",
    "lesson_11_cross_compilation",
    "lesson_13_tuples",
    "lesson_14_types",
]]

# Several tutorials also depend on //tools:halide_image_io and require access to images.
# These must be run with using --run_under='cd tutorial && ' so that they start
# in the appropriate directory to access the images, e.g.
#
#   bazel run --run_under='cd tutorial && ' tutorial/lesson_02_input_image
#
[cc_binary(
    name = t,
    srcs = [
        "%s.cpp" % t,
    ],
    copts = halide_language_copts() + _IGNORE_UNUSED,
    data = glob(["images/*.png"]),
    linkopts = halide_language_linkopts(),
    deps = [
        ":clock",
        "@halide//:language",
        "@halide//tools:halide_image_io",
    ],
) for t in [
    "lesson_02_input_image",
    "lesson_07_multi_stage_pipelines",
    "lesson_09_update_definitions",
    "lesson_12_using_the_gpu",
]]

# For Lesson 10, we must build a binary, execute it, and depend on the output from that execution.
cc_binary(
    name = "lesson_10_aot_compilation_generate_gen",
    srcs = ["lesson_10_aot_compilation_generate.cpp"],
    copts = halide_language_copts() + _IGNORE_UNUSED,
    linkopts = halide_language_linkopts(),
    deps = ["@halide//:language"],
)

genrule(
    name = "lesson_10_aot_compilation_generate_exec",
    outs = [
        "lesson_10_halide.a",
        "lesson_10_halide.h",
    ],
    cmd = "$(location :lesson_10_aot_compilation_generate_gen) $(@D)/",
    tools = [":lesson_10_aot_compilation_generate_gen"],
)

cc_library(
    name = "lesson_10_aot_compilation_generate_lib",
    srcs = [":lesson_10_aot_compilation_generate_exec"],
    hdrs = ["lesson_10_halide.h"],
    includes = ["."],
    linkstatic = 1,
)

cc_binary(
    name = "lesson_10_aot_compilation_run",
    srcs = [
        "lesson_10_aot_compilation_run.cpp",
    ],
    linkopts = halide_runtime_linkopts(),
    deps = [
        ":lesson_10_aot_compilation_generate_lib",
        "@halide//:language",
    ],
)

# For Lesson 15, we manually build and use a Generator.
# (You normally would use the halide_generator()/halide_library_from_generator() rules to do this for you.)
cc_binary(
    name = "lesson_15_generate",
    srcs = ["lesson_15_generators.cpp"],
    copts = halide_language_copts() + _IGNORE_UNUSED,
    linkopts = halide_language_linkopts(),
    deps = [
        "@halide//:language",
        "@halide//tools:gengen",
    ],
)

# lesson_15_generators_usage must be run with using --run_under='cd tutorial && '
# so it starts in the appropriate directory to run ./lesson_15_generate, e.g.
#
#   bazel run --run_under='cd tutorial && ' tutorial/lesson_15_generators_usage
#
sh_binary(
    name = "lesson_15_generators_usage",
    srcs = ["lesson_15_generators_usage.sh"],
    data = [":lesson_15_generate"],
)

# For Lesson 16, we use halide_generator()/halide_library_from_generator() to simplify Generator usage
halide_generator(
    name = "brighten_generator",
    srcs = ["lesson_16_rgb_generate.cpp"],
    includes = ["."],
)

[halide_library_from_generator(
    name = "brighten_%s" % layout,
    generator = ":brighten_generator",
    generator_args = "layout=%s" % layout,
    includes = ["."],
) for layout in [
    "planar",
    "interleaved",
    "either",
    "specialized",
]]

cc_binary(
    name = "lesson_16_rgb_run",
    srcs = [
        "lesson_16_rgb_run.cpp",
    ],
    copts = _IGNORE_UNUSED,
    linkopts = halide_runtime_linkopts(),
    deps = [
        ":brighten_either",
        ":brighten_interleaved",
        ":brighten_planar",
        ":brighten_specialized",
        ":clock",
    ],
)

# These "tests" don't really do anything other than ensure that each tutorial
# will build and execute correctly (but that's all we need)
[exec_test(
    name = "%s_test" % t,
    cmd = [
        "echo Testing %s..." % t,
        "cd tutorial",
        "./%s" % t,
    ],
    data = [":%s" % t] + glob(["images/*.png"]),
) for t in [
    "lesson_01_basics",
    "lesson_02_input_image",
    "lesson_03_debugging_1",
    "lesson_04_debugging_2",
    "lesson_05_scheduling_1",
    "lesson_06_realizing_over_shifted_domains",
    "lesson_07_multi_stage_pipelines",
    "lesson_08_scheduling_2",
    "lesson_09_update_definitions",
    "lesson_10_aot_compilation_run",
    "lesson_11_cross_compilation",
    "lesson_12_using_the_gpu",
    "lesson_13_tuples",
    "lesson_14_types",
    "lesson_15_generators_usage",
    "lesson_16_rgb_run",
]]

filegroup(
    name = "distrib_files",
    srcs = glob([
        "*.h",
        "*.cpp",
        "*.sh",
    ]),
    visibility = ["//:__subpackages__"],  # TODO add @halide when https://github.com/bazelbuild/bazel/issues/1248 is fixed
)

filegroup(
    name = "distrib_images",
    srcs = glob(["images/*.png"]),
    visibility = ["//:__subpackages__"],  # TODO add @halide when https://github.com/bazelbuild/bazel/issues/1248 is fixed
)

filegroup(
    name = "distrib_figures",
    srcs = glob([
        "figures/*.gif",
        "figures/*.jpg",
        "figures/*.mp4",
    ]),
    visibility = ["//:__subpackages__"],  # TODO add @halide when https://github.com/bazelbuild/bazel/issues/1248 is fixed
)
