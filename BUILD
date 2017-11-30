# TODO apps/HelloAndroidCamera2
# TODO apps/HelloAndroidGL
# TODO apps/HelloHexagon
# TODO apps/HelloMatlab
# TODO apps/linear_algebra
# TODO apps/opengl_demo
# TODO apps/openglcompute
# TODO apps/simd_op_check

package(
    default_visibility = ["//visibility:private"],
)

load("@bazel_tools//tools/build_defs/pkg:pkg.bzl", "pkg_tar")
load("@halide//:halide.bzl", "halide_config_settings", "halide_language_copts", "halide_library_runtimes")
load("@halide//:tools/halide_internal_build_defs.bzl", "gen_runtime")
load("@llvm//:llvm_internal_build_defs.bzl", "get_llvm_version", "get_llvm_enabled_components", "get_llvm_linkopts")

halide_config_settings()

halide_library_runtimes()

filegroup(
    name = "runtime_headers",
    srcs = glob(["src/runtime/Halide*.h"]),
)

filegroup(
    name = "language_headers",
    srcs = glob(
        ["src/*.h"],
        exclude = [
            # We must not export files that reference LLVM headers,
            # as clients using prebuilt halide libraries won't
            # have access to them. (No client should need those files,
            # anyway.)
            "src/CodeGen_Internal.h",
            "src/HalideFooter.h",
            "src/LLVM_Headers.h",
        ],
    ),
)

filegroup(
    name = "runtime_files",
    srcs = glob(
        [
            "src/runtime/*",
        ],
    ),
)

genrule(
    name = "build_single_language_header",
    srcs = [
        ":language_headers",
        ":runtime_headers",
        "src/HalideFooter.h",
    ],
    # TODO moving into 'generated' subfolder as workaround for https://github.com/bazelbuild/bazel/issues/1248
    outs = ["generated/Halide.h"],
    # :runtime_headers needs to be made available to the sandbox,
    # but we only want to use the ones referenced indirectly by
    # :language_headers.
    cmd = "$(location @halide//tools:build_halide_h) $(locations :language_headers) $(location src/HalideFooter.h) > $@",
    tools = ["@halide//tools:build_halide_h"],
)

cc_library(
    name = "single_language_header_lib",
    srcs = [":build_single_language_header"],
    copts = halide_language_copts(),
    includes = ["generated"],
    linkstatic = 1,
)

_RUNTIME_CPP_COMPONENTS = [
    "aarch64_cpu_features",
    "alignment_128",
    "alignment_32",
    "android_clock",
    "android_host_cpu_count",
    "android_io",
    "android_opengl_context",
    "android_tempfile",
    "arm_cpu_features",
    "buffer_t",
    "cache",
    "can_use_target",
    "cuda",
    "destructors",
    "device_interface",
    "errors",
    "fake_thread_pool",
    "float16_t",
    "gcd_thread_pool",
    "gpu_device_selection",
    "hexagon_cpu_features",
    "hexagon_host",
    "ios_io",
    "linux_clock",
    "linux_host_cpu_count",
    "linux_opengl_context",
    "matlab",
    "metadata",
    "metal",
    "metal_objc_arm",
    "metal_objc_x86",
    "mingw_math",
    "mips_cpu_features",
    "module_aot_ref_count",
    "module_jit_ref_count",
    "msan",
    "msan_stubs",
    "old_buffer_t",
    "opencl",
    "opengl",
    "openglcompute",
    "osx_clock",
    "osx_get_symbol",
    "osx_host_cpu_count",
    "osx_opengl_context",
    "posix_allocator",
    "posix_clock",
    "posix_error_handler",
    "posix_get_symbol",
    "posix_io",
    "posix_print",
    "posix_tempfile",
    "posix_threads",
    "powerpc_cpu_features",
    "prefetch",
    "profiler",
    "profiler_inlined",
    "qurt_allocator",
    "qurt_hvx",
    "qurt_init_fini",
    "qurt_thread_pool",
    "runtime_api",
    "ssp",
    "thread_pool",
    "to_string",
    "tracing",
    "windows_clock",
    "windows_cuda",
    "windows_get_symbol",
    "windows_io",
    "windows_opencl",
    "windows_tempfile",
    "windows_threads",
    "write_debug_image",
    "x86_cpu_features",
]

_RUNTIME_LL_COMPONENTS = [
    "aarch64",
    "arm",
    "arm_no_neon",
    "hvx_64",
    "hvx_128",
    "mips",
    "posix_math",
    "powerpc",
    "ptx_dev",
    "win32_math",
    "x86",
    "x86_avx",
    "x86_sse41",
]

_RUNTIME_NVIDIA_BITCODE_COMPONENTS = [
    "compute_20",
    "compute_30",
    "compute_35",
]

_RUNTIME_HEADER_COMPONENTS = [
    "HalideRuntimeCuda",
    "HalideRuntime",
    "HalideRuntimeHexagonHost",
    "HalideRuntimeMetal",
    "HalideRuntimeOpenCL",
    "HalideRuntimeOpenGLCompute",
    "HalideRuntimeOpenGL",
]

_RUNTIME_INLINED_C_COMPONENTS = [
    "buffer_t",
]

gen_runtime(
    "halide_runtime_components",
    _RUNTIME_CPP_COMPONENTS,
    _RUNTIME_LL_COMPONENTS,
    _RUNTIME_NVIDIA_BITCODE_COMPONENTS,
    _RUNTIME_HEADER_COMPONENTS,
    _RUNTIME_INLINED_C_COMPONENTS,
)

_ENABLED_COMPONENTS = get_llvm_enabled_components() + [
    "WITH_METAL",
    "WITH_OPENCL",
    "WITH_OPENGL",
]

# Note that this target does *not* build a fully-static libHalide.a;
# Bazel doesn't currently have a way to actually enforce that all static
# dependencies are lumped in (e.g. LLVM in our case). We use some rather
# ugly scripting to do that as a separate target.
cc_library(
    name = "lib_halide_internal",
    srcs = glob(
        [
            "src/*.cpp",
            "src/*.h",
        ],
    ) + [
        ":runtime_headers",
    ],
    copts = [
        "-DCOMPILING_HALIDE",
        "-DLLVM_VERSION=" + get_llvm_version(),
    ] + [
        "-D%s" % component
        for component in _ENABLED_COMPONENTS
    ] + halide_language_copts(),
    linkstatic = 1,
    deps = [
        ":halide_runtime_components",
        "@llvm//:llvm",
    ],
)

genrule(
    name = "halide_fully_static",
    srcs = [
        ":lib_halide_internal",
        "@llvm//:llvm_static_libs",  # static_link.sh needs file-path access to these
    ],
    outs = [
        "libHalide.a",
    ],
    cmd = " ".join([
        "bash",
        "$(location //tools:static_link.sh)",
        "$(@D)",
        "\"$(locations :lib_halide_internal)\"",  # TODO: locations because -c opt produces both .a and .pic.a
        "$@",
        "\"$(CC) $(CC_FLAGS)\"",
        "\"%s\"" % " ".join(get_llvm_linkopts()),
    ]),
    tools = [
        "//tools:static_link.sh",
        "//tools/defaults:crosstool",  # This is necessary to access $(CC); see https://github.com/bazelbuild/bazel/issues/2058
    ],
)

# TODO: this is somewhat useless unless we fix rpath on mac,
# which doesn't appear to be feasible right now?
cc_binary(
    name = "libHalide.so",
    # TODO: this *should* be correct and necessary to make a fully-self-contained
    # .so file (and on OSX, it seems to); something is amiss on Linux, so
    # disabled for now, since the downstream distro rules don't use it (yet).
    # linkopts = ["-static"],
    linkshared = 1,
    deps = [":lib_halide_internal"],
)

cc_library(
    name = "language",
    copts = halide_language_copts(),
    visibility = ["//visibility:public"],
    deps = [
        ":lib_halide_internal",
        ":runtime",
        ":single_language_header_lib",
    ],
)

# Android needs the -llog flag for __android_log_print
_ANDROID_RUNTIME_LINKOPTS = [
    "-ldl",
    "-llog",
]

_WINDOWS_RUNTIME_LINKOPTS = [
    # Nothing
]

_DEFAULT_RUNTIME_LINKOPTS = [
    "-ldl",
]

# This allows runtime files to pull in the definition of buffer_t,
# plus definitions of functions that can be replaced by hosting applications.
cc_library(
    name = "runtime",
    hdrs = [":runtime_headers"],
    copts = halide_language_copts(),
    includes = ["src/runtime"],
    linkopts = select({
        # There isn't (yet) a good way to make a config that is "Any Android",
        # so we're forced to specialize on all supported Android CPU configs.
        "@halide//:halide_config_arm_32_android": _ANDROID_RUNTIME_LINKOPTS,
        "@halide//:halide_config_arm_64_android": _ANDROID_RUNTIME_LINKOPTS,
        "@halide//:halide_config_x86_32_android": _ANDROID_RUNTIME_LINKOPTS,
        "@halide//:halide_config_x86_64_android": _ANDROID_RUNTIME_LINKOPTS,
        "@halide//:halide_config_x86_64_windows": _WINDOWS_RUNTIME_LINKOPTS,
        "//conditions:default": _DEFAULT_RUNTIME_LINKOPTS,
    }),
    visibility = ["//visibility:public"],
)

# Use a genrule to rename-on-the-fly "halide_distrib.BUILD" -> "BUILD"
# when building the distribution.
genrule(
    name = "halide_distrib_BUILD",
    srcs = [
        "//tools:halide_distrib.BUILD",
    ],
    outs = ["generated/BUILD"],
    cmd = "cp $(location //tools:halide_distrib.BUILD) $@",
)

genrule(
    name = "halide_distrib_halide_bzl",
    srcs = ["halide.bzl"],
    outs = ["generated/halide.bzl"],
    # We don't (currently) need to transform halide.bzl; leaving this here for now
    # as I suspect we might need to before all is said and done.
    # cmd = "cat halide.bzl | sed -e 's|@halide//|//|g' > $@",
    cmd = "cp $(location halide.bzl) $@",
)

_DISTRIB_PKG = [
    (
        "",
        [
            ":halide_distrib_halide_bzl",
            "README.md",
            ":halide_distrib_BUILD",
        ],
    ),
    (
        "bin",
        [":libHalide.so"],
    ),
    (
        "include",
        [
            ":build_single_language_header",
            ":runtime_headers",
        ],
    ),
    (
        "lib",
        [":halide_fully_static"],
    ),
    (
        "tools",
        ["//tools:distrib_files"],
    ),
    (
        "tutorial",
        ["//tutorial:distrib_files"],
    ),
    (
        "tutorial/figures",
        ["//tutorial:distrib_figures"],
    ),
    (
        "tutorial/images",
        ["//tutorial:distrib_images"],
    ),
]

[pkg_tar(
    name = "distrib_%s" % path.replace("/", "_"),
    srcs = targets,
    mode = "0644",
    package_dir = path,
) for path, targets in _DISTRIB_PKG]

pkg_tar(
    name = "halide",
    extension = "tar.gz",
    package_dir = "halide",
    deps = [":distrib_%s" % path.replace("/", "_") for path, targets in _DISTRIB_PKG],
)

# TODO: should this be moved to a BUILD file in src/runtime?
cc_library(
    name = "mini_opengl",
    testonly = 1,
    hdrs = ["src/runtime/mini_opengl.h"],
    copts = halide_language_copts(),
    includes = [
        "src",
        "src/runtime",
    ],
    visibility = ["//test:__subpackages__"],  # TODO add @halide when https://github.com/bazelbuild/bazel/issues/1248 is fixed
)

# TODO: should this be moved to a BUILD file in src/runtime?
cc_library(
    name = "device_interface",
    testonly = 1,
    hdrs = ["src/runtime/device_interface.h"],
    copts = halide_language_copts(),
    includes = ["src/runtime"],
    visibility = ["//test:__subpackages__"],  # TODO add @halide when https://github.com/bazelbuild/bazel/issues/1248 is fixed
)

cc_library(
    name = "internal_test_includes",
    testonly = 1,
    hdrs = [
        ":language_headers",
        ":runtime_headers",
    ],
    copts = halide_language_copts(),
    includes = ["src"],
    visibility = ["//test:__subpackages__"],  # TODO add @halide when https://github.com/bazelbuild/bazel/issues/1248 is fixed
)

cc_library(
    name = "internal_halide_generator_glue",
    srcs = ["@halide//tools:gengen"],
    copts = halide_language_copts(),
    linkstatic = 1,
    visibility = ["//visibility:public"],
    deps = [
        ":language",
        ":single_language_header_lib",
    ],
)

# Header-only library to let clients to use Halide::Buffer at runtime.
# (Generators should never need to use this library.)
cc_library(
    name = "halide_buffer",
    hdrs = glob(["include/HalideBuffer*.h"]),
    includes = ["include"],
    visibility = ["//visibility:public"],
)
