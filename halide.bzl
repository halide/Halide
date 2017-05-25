def halide_language_copts():
  _common_opts = [
      "-DGOOGLE_PROTOBUF_NO_RTTI",
      "-fPIC",
      "-fno-rtti", 
      "-std=c++11", 
      "-Wno-conversion",
      "-Wno-sign-compare",
  ]
  _clangy_opts = [
      "$(STACK_FRAME_UNLIMITED)", 
      "-fno-exceptions", 
      "-funwind-tables",
      "-fvisibility-inlines-hidden", 
  ]
  _msvc_opts = [
      "-D_CRT_SECURE_NO_WARNINGS",
      # Linking with LLVM on Windows requires multithread+DLL CRT
      "/MD",
  ]
  return _common_opts + select({
      "@halide//:halide_platform_config_x64_windows_msvc":
          _msvc_opts,
      "@halide//:halide_platform_config_x64_windows":
          ["/error_please_set_cpu_and_host_cpu_x64_windows_msvc"],
      "@halide//:halide_platform_config_darwin":
          _clangy_opts,
      "@halide//:halide_platform_config_darwin_x86_64":
          _clangy_opts,
      "//conditions:default":
          _clangy_opts,
  })

def halide_language_linkopts():
  _linux_opts = [
     "-rdynamic", 
     "-ldl", 
     "-lpthread", 
     "-lz"
  ]
  _osx_opts = [
     "-Wl,-stack_size", 
      "-Wl,1000000"
  ]
  _msvc_opts = []  # TODO
  return select({
      "@halide//:halide_platform_config_x64_windows_msvc":
          _msvc_opts,
      "@halide//:halide_platform_config_x64_windows":
          ["/error_please_set_cpu_and_host_cpu_x64_windows_msvc"],
      "@halide//:halide_platform_config_darwin":
          _osx_opts,
      "@halide//:halide_platform_config_darwin_x86_64":
          _osx_opts,
      "//conditions:default":
          _linux_opts,
  })


def halide_runtime_linkopts():
  _clangy_opts = [
      "-ldl",
      "-lpthread",
  ]
  _msvc_opts = []  # TODO
  return select({
      "@halide//:halide_platform_config_x64_windows_msvc":
          _msvc_opts,
      "@halide//:halide_platform_config_x64_windows":
          ["/error_please_set_cpu_and_host_cpu_x64_windows_msvc"],
      "@halide//:halide_platform_config_darwin":
          _clangy_opts,
      "@halide//:halide_platform_config_darwin_x86_64":
          _clangy_opts,
      "//conditions:default":
          _clangy_opts,
  })


def halide_opengl_linkopts():
  _linux_opts = ["-lGL", "-lX11"]
  _osx_opts = ["-framework OpenGL"]
  _msvc_opts = []  # TODO
  return select({
      "@halide//:halide_platform_config_x64_windows_msvc":
          _msvc_opts,
      "@halide//:halide_platform_config_x64_windows":
          ["/error_please_set_cpu_and_host_cpu_x64_windows_msvc"],
      "@halide//:halide_platform_config_darwin":
          _osx_opts,
      "@halide//:halide_platform_config_darwin_x86_64":
          _osx_opts,
      "//conditions:default":
          _linux_opts,
  })


# (halide-target-base, cpu, android-cpu, ios-cpu)
_HALIDE_TARGET_CONFIG_INFO = [
    # Android
    ("arm-32-android", None, "armeabi-v7a", None),
    ("arm-64-android", None, "arm64-v8a", None),
    ("x86-32-android", None, "x86", None),
    ("x86-64-android", None, "x86_64", None),
    # iOS
    ("arm-32-ios", None, None, "armv7"),
    ("arm-64-ios", None, None, "arm64"),
    # OSX
    ("x86-32-osx", None, None, "i386"),
    ("x86-64-osx", None, None, "x86_64"),
    # Linux
    ("arm-64-linux", "arm", None, None),
    ("powerpc-64-linux", "ppc", None, None),
    ("x86-64-linux", "k8", None, None),
    ("x86-32-linux", "piii", None, None),
    # Windows
    ("x86-64-windows", "x64_windows_msvc", None, None),
    # Special case: Android-ARMEABI. Note that we are using an illegal Target
    # string for Halide; this is intentional. It allows us to add another
    # config_setting to match the armeabi-without-v7a required for certain build
    # scenarios; we special-case this in _select_multitarget to translate it
    # back into a legal Halide target.
    #
    # Note that this won't produce a build that is useful (it will SIGILL on
    # non-v7a hardware), but isn't intended to be useful for anything other
    # than allowing certain builds to complete.
    ("armeabi-32-android", "armeabi", "armeabi", None),
]
# TODO: add conditions appropriate for other targets/cpus

_HALIDE_TARGET_MAP_DEFAULT = {
    "x86-64-osx": [
        "x86-64-osx-sse41-avx-avx2-fma",
        "x86-64-osx-sse41-avx",
        "x86-64-osx-sse41",
        "x86-64-osx",
    ],
    # "x86-64-windows": [
    #     "x86-64-windows-sse41-avx-avx2-fma",
    #     "x86-64-windows-sse41-avx",
    #     "x86-64-windows-sse41",
    #     "x86-64-windows",
    # ],
    "x86-64-linux": [
        "x86-64-linux-sse41-avx-avx2-fma",
        "x86-64-linux-sse41-avx",
        "x86-64-linux-sse41",
        "x86-64-linux",
    ],
    "x86-32-linux": [
        "x86-32-linux-sse41",
        "x86-32-linux",
    ],
}


def halide_library_default_target_map():
  return _HALIDE_TARGET_MAP_DEFAULT


_HALIDE_RUNTIME_OVERRIDES = {
    # Empty placeholder for now; we may add target-specific
    # overrides here in the future.
}


def halide_config_settings():
  """Define config_settings for halide_library.

       These settings are used to distinguish build targets for
       halide_library() based on target CPU and configs. This is provided
       to allow algorithmic generation of the config_settings based on
       internal data structures; it should not be used outside of Halide.

  """
  cpus = [
      "darwin",
      "darwin_x86_64",
      "x64_windows_msvc",
      "x64_windows",
  ]
  for cpu in cpus:
    native.config_setting(
        name="halide_platform_config_%s" % cpu,
        values={
            "cpu": cpu,
        },
        visibility=["//visibility:public"])

  for base_target, cpu, android_cpu, ios_cpu in _HALIDE_TARGET_CONFIG_INFO:
    if android_cpu == None:
      # "armeabi" is the default value for --android_cpu and isn't considered legal
      # here, so we use the value to assume we aren't building for Android.
      android_cpu = "armeabi"
    if ios_cpu == None:
      # The default value for --ios_cpu is "x86_64", i.e. for the 64b OS X simulator.
      # Assuming that the i386 version of the simulator will be used along side
      # arm32 apps, we consider this value to mean the flag was unspecified; this
      # won't work for 32 bit simulator builds for A6 or older phones.
      ios_cpu = "x86_64"
    if cpu != None:
      values = {
          "cpu": cpu,
          "android_cpu": android_cpu,
          "ios_cpu": ios_cpu,
      }
    else:
      values = {
          "android_cpu": android_cpu,
          "ios_cpu": ios_cpu,
      }
    native.config_setting(
        name=_config_setting_name(base_target),
        values=values,
        visibility=["//visibility:public"])

  # Config settings for Sanitizers (currently, only MSAN)
  native.config_setting(
      name="halide_config_msan",
      values={"compiler": "msan"},
      visibility=["//visibility:public"])


# Alphabetizes the features part of the target to make sure they always match no
# matter the concatenation order of the target string pieces.
def _canonicalize_target(halide_target):
  if halide_target == "host":
    return halide_target
  if "," in halide_target:
    fail("Multitarget may not be specified here")
  tokens = halide_target.split("-")
  if len(tokens) < 3:
    fail("Illegal target: %s" % halide_target)
  # rejoin the tokens with the features sorted
  return "-".join(tokens[0:3] + sorted(tokens[3:]))


# Converts comma and dash separators to underscore and alphabetizes
# the features part of the target to make sure they always match no
# matter the concatenation order of the target string pieces.
def _halide_target_to_bazel_rule_name(multitarget):
  subtargets = multitarget.split(",")
  subtargets = [_canonicalize_target(st).replace("-", "_") for st in subtargets]
  return "_".join(subtargets)


def _config_setting_name(halide_target):
  """Take a Halide target string and converts to a unique name suitable for a Bazel config_setting."""
  if "," in halide_target:
    fail("Multitarget may not be specified here: %s" % halide_target)
  tokens = halide_target.split("-")
  if len(tokens) != 3:
    fail("Unexpected halide_target form: %s" % halide_target)
  halide_arch = tokens[0]
  halide_bits = tokens[1]
  halide_os = tokens[2]
  return "halide_config_%s_%s_%s" % (halide_arch, halide_bits, halide_os)


def _config_setting(halide_target):
  return "@halide//:%s" % _config_setting_name(halide_target)


_output_extensions = {
    "static_library": ("a", False),
    "o": ("o", False),
    "h": ("h", False),
    "cpp_stub": ("stub.h", False),
    "assembly": ("s.txt", True),
    "bitcode": ("bc", True),
    "stmt": ("stmt", True),
    "html": ("html", True),
    "cpp": ("cpp", True),
}


def _gengen_outputs(filename, halide_target, outputs):
  new_outputs = {}
  for o in outputs:
    if o not in _output_extensions:
      fail("Unknown output: " + o)
    ext, is_multiple = _output_extensions[o]
    if is_multiple and len(halide_target) > 1:
      # Special handling needed for ".s.txt" and similar: the suffix from the
      # is_multiple case always goes before the final .
      # (i.e. "filename.s_suffix.txt", not "filename_suffix.s.txt")
      # -- this is awkward, but is what Halide does, so we must match it.
      pieces = ext.rsplit(".", 1)
      extra = (".%s" % pieces[0]) if len(pieces) > 1 else ""
      ext = pieces[-1]
      for h in halide_target:
        new_outputs[o + h] = "%s%s_%s.%s" % (
            filename, extra, _canonicalize_target(h).replace("-", "_"), ext)
    else:
      new_outputs[o] = "%s.%s" % (filename, ext)
  return new_outputs


def _gengen_impl(ctx):
  if _has_dupes(ctx.attr.outputs):
    fail("Duplicate values in outputs: " + str(ctx.attr.outputs))

  if not ctx.attr.generator_closure.generator_name:
    fail("generator_name must be specified")

  remaps = [".s=.s.txt"]
  halide_target = ctx.attr.halide_target
  if "windows" in halide_target[-1] and not "mingw" in halide_target[-1]:
    remaps += [".obj=.o", ".lib=.a"]
  if ctx.attr.sanitizer:
    halide_target = []
    for t in ctx.attr.halide_target:
      ct = _canonicalize_target("%s-%s" % (t, ctx.attr.sanitizer))
      halide_target += [ct]
      remaps += ["%s=%s" % (ct.replace("-", "_"), t.replace("-", "_"))]

  outputs = [
      ctx.new_file(f)
      for f in _gengen_outputs(
          ctx.attr.filename,
          ctx.attr.halide_target,  # *not* halide_target
          ctx.attr.outputs).values()
  ]

  arguments = ["-o", outputs[0].dirname]
  if ctx.attr.generate_runtime:
    arguments += ["-r", ctx.attr.filename]
    if len(halide_target) > 1:
      fail("Only one halide_target allowed here")
    if ctx.attr.halide_function_name:
      fail("halide_function_name not allowed here")
  else:
    arguments += ["-g", ctx.attr.generator_closure.generator_name]
    if ctx.attr.filename:
      arguments += ["-n", ctx.attr.filename]
    if ctx.attr.halide_function_name:
      arguments += ["-f", ctx.attr.halide_function_name]

  if ctx.attr.outputs:
    arguments += ["-e", ",".join(ctx.attr.outputs)]
    arguments += ["-x", ",".join(remaps)]
  arguments += ["target=%s" % ",".join(halide_target)]
  if ctx.attr.halide_generator_args:
    arguments += ctx.attr.halide_generator_args.split(" ")

  env = {
      "HL_DEBUG_CODEGEN": str(ctx.attr.debug_codegen_level),
  }
  ctx.action(
      # If you need to force the tools to run locally (e.g. for experimentation),
      # uncomment this line.
      # execution_requirements={"local":"1"},
      arguments=arguments,
      env=env,
      # TODO: files_to_run is undocumented but (apparently) reliable
      executable=ctx.attr.generator_closure.generator_binary.files_to_run.executable,
      mnemonic="ExecuteHalideGenerator",
      outputs=outputs,
      progress_message="Executing generator %s with target (%s) args (%s)..." %
      (ctx.attr.generator_closure.generator_name,
       ",".join(halide_target),
       ctx.attr.halide_generator_args))


_gengen = rule(
    implementation=_gengen_impl,
    attrs={
        "debug_codegen_level":
            attr.int(),
        "filename":
            attr.string(),
        "generate_runtime":
            attr.bool(default=False),
        "generator_closure":
            attr.label(
                cfg="host", providers=["generator_binary", "generator_name"]),
        "halide_target":
            attr.string_list(),
        "halide_function_name":
            attr.string(),
        "halide_generator_args":
            attr.string(),
        "outputs":
            attr.string_list(),
        "sanitizer":
            attr.string(),
    },
    outputs=_gengen_outputs,
    output_to_genfiles=True)


def _add_target_features(target, features):
  if "," in target:
    fail("Cannot use multitarget here")
  new_target = target.split("-")
  for f in features:
    if f and f not in new_target:
      new_target += [f]
  return "-".join(new_target)


def _has_dupes(some_list):
  clean = list(set(some_list))
  return sorted(some_list) != sorted(clean)


def _select_multitarget(base_target,
                        halide_target_features,
                        halide_target_map):
  if base_target == "armeabi-32-android":
    base_target = "arm-32-android"
  wildcard_target = halide_target_map.get("*")
  if wildcard_target:
    expected_base = "*"
    targets = wildcard_target
  else:
    expected_base = base_target
    targets = halide_target_map.get(base_target, [base_target])

  multitarget = []
  for t in targets:
    if not t.startswith(expected_base):
      fail(
          "target %s does not start with expected target %s for halide_target_map"
          % (t, expected_base))
    t = t[len(expected_base):]
    if t.startswith("-"):
      t = t[1:]
    # Check for a "match all base targets" entry:
    multitarget.append(_add_target_features(base_target, t.split("-")))

  # Add the extra features (if any).
  if halide_target_features:
    multitarget = [
        _add_target_features(t, halide_target_features) for t in multitarget
    ]

  # Finally, canonicalize all targets
  multitarget = [_canonicalize_target(t) for t in multitarget]
  return multitarget


def _gengen_closure_impl(ctx):
  return struct(
      generator_binary=ctx.attr.generator_binary,
      generator_name=ctx.attr.halide_generator_name)


_gengen_closure = rule(
    implementation=_gengen_closure_impl,
    attrs={
        "generator_binary":
            attr.label(
                executable=True, allow_files=True, mandatory=True, cfg="host"),
        "halide_generator_name":
            attr.string(),
    })

def _discard_useless_features(halide_target_features = []):
  # Discard target features which do not affect the contents of the runtime.
  useless_features = set(["user_context", "no_asserts", "no_bounds_query", "profile"])
  return sorted(list(set([f for f in halide_target_features if f not in useless_features])))

def _halide_library_runtime_target_name(halide_target_features = []):
  return "_".join(["halide_library_runtime"] + _discard_useless_features(halide_target_features))

def _define_halide_library_runtime(halide_target_features = []):
  target_name = _halide_library_runtime_target_name(halide_target_features)

  if not native.existing_rule("halide_library_runtime_generator"):
    halide_generator(
        name="halide_library_runtime_generator",
        srcs=[],
        deps=[],
        visibility=["//visibility:private"])
  condition_deps = {}
  for base_target, _, _, _ in _HALIDE_TARGET_CONFIG_INFO:
    if base_target == "armeabi-32-android":
      # For armeabi-32-android, just generate an arm-32-android runtime
      halide_target = "arm-32-android"
    else:
      halide_target = base_target
    sub_name = (target_name + "_" +
                _halide_target_to_bazel_rule_name(base_target))
    _gengen(
        name=sub_name,
        filename=sub_name,
        generate_runtime=True,
        generator_closure="halide_library_runtime_generator_closure",
        halide_target=["-".join([halide_target] + _discard_useless_features(halide_target_features))],
        sanitizer=select({
            "@halide//:halide_config_msan": "msan",
            "//conditions:default": "",
        }),
        outputs=["o"],
        tags=["manual"],
        visibility=[
            "@halide//:__subpackages__",
        ]
    )
    condition_deps[_config_setting(base_target)] = [":%s.o" % sub_name]

  native.cc_library(
      name=target_name,
      srcs=select(condition_deps),
      tags=["manual"],
      visibility=["//visibility:public"])

  return target_name

def _standard_library_runtime_features():
  standard_features = [
      [],
      ["c_plus_plus_name_mangling"],
      ["cuda"],
      ["cuda", "matlab"],
      ["hvx_64"],
      ["hvx_128"],
      ["matlab"],
      ["metal"],
      ["msan"],
      ["opengl"],
  ]
  return [f for f in standard_features] + [f + ["debug"] for f in standard_features]

def _standard_library_runtime_names():
  return set([_halide_library_runtime_target_name(f) for f in _standard_library_runtime_features()])

def halide_library_runtimes():
  runtime_package = ""
  if PACKAGE_NAME != runtime_package:
    fail("halide_library_runtimes can only be used from package '%s' (this is %s)" % (runtime_package, PACKAGE_NAME))
  unused = [_define_halide_library_runtime(f) for f in _standard_library_runtime_features()]
  unused = unused  # unused variable


def halide_generator(name,
                     srcs,
                     copts=[],
                     deps=[],
                     generator_name="",
                     includes=[],
                     tags=[],
                     visibility=None):
  if not name.endswith("_generator"):
    fail("halide_generator rules must end in _generator")

  if not generator_name:
    generator_name = name[:-10]  # strip "_generator" suffix

  native.cc_library(
      name="%s_library" % name,
      srcs=srcs,
      alwayslink=1,
      copts=copts + halide_language_copts(),
      deps=set(["@halide//:language"] + deps),
      tags=["manual"] + tags,
      visibility=["//visibility:private"])

  native.cc_binary(
      name="%s_binary" % name,
      copts=copts + halide_language_copts(),
      linkopts=halide_language_linkopts(),
      deps=[
          ":%s_library" % name,
          "@halide//:internal_halide_generator_glue",
      ],
      tags=["manual"] + tags,
      visibility=["//visibility:private"])
  _gengen_closure(
      name="%s_closure" % name,
      generator_binary="%s_binary" % name,
      halide_generator_name=generator_name,
      visibility=["//visibility:private"])

  # If srcs is empty, we're building the halide-library-runtime,
  # which has no stub: just skip it.
  stub_gen_hdrs_target = []
  if srcs:
    # The specific target doesn't matter (much), but we need
    # something that is valid, so uniformly choose first entry
    # so that build product cannot vary by build host
    stub_header_target = _select_multitarget(
        base_target=_HALIDE_TARGET_CONFIG_INFO[0][0],
        halide_target_features=[],
        halide_target_map={})
    _gengen(
        name="%s_stub_gen" % name,
        filename=name[:-10],  # strip "_generator" suffix
        generator_closure=":%s_closure" % name,
        halide_target=stub_header_target,
        outputs=["cpp_stub"],
        tags=tags,
        visibility=["//visibility:private"])
    stub_gen_hdrs_target = [":%s_stub_gen" % name]

  native.cc_library(
      name=name,
      alwayslink=1,
      hdrs=stub_gen_hdrs_target,
      deps=[
          ":%s_library" % name,
          "@halide//:language"
      ],
      copts=copts + halide_language_copts(),
      includes=includes,
      visibility=visibility,
      tags=["manual"] + tags)


def halide_library_from_generator(name,
                                  generator,
                                  debug_codegen_level=0,
                                  deps=[],
                                  extra_outputs=[],
                                  function_name=None,
                                  generator_args="",
                                  halide_target_features=[],
                                  halide_target_map=halide_library_default_target_map(),
                                  includes=[],
                                  namespace=None,
                                  tags=[],
                                  visibility=None):
  if not function_name:
    function_name = name

  if namespace:
    function_name = "%s::%s" % (namespace, function_name)

  # Escape backslashes and double quotes.
  generator_args = generator_args.replace("\\", '\\\\"').replace('"', '\\"')

  if _has_dupes(halide_target_features):
    fail("Duplicate values in halide_target_features: %s" %
         str(halide_target_features))
  if _has_dupes(extra_outputs):
    fail("Duplicate values in extra_outputs: %s" % str(extra_outputs))

  full_halide_target_features = sorted(list(set(halide_target_features + ["c_plus_plus_name_mangling", "no_runtime"])))
  user_halide_target_features = sorted(list(set(halide_target_features)))

  outputs = extra_outputs
  # TODO: yuck. hacky for apps/c_backend.
  if not "cpp" in outputs:
    outputs += ["static_library"]

  generator_closure = "%s_closure" % generator

  condition_deps = {}
  for base_target, _, _, _ in _HALIDE_TARGET_CONFIG_INFO:
    multitarget = _select_multitarget(
        base_target=base_target,
        halide_target_features=full_halide_target_features,
        halide_target_map=halide_target_map)
    arch = _halide_target_to_bazel_rule_name(base_target)
    sub_name = "%s_%s" % (name, arch)
    _gengen(
        name=sub_name,
        filename=sub_name,
        halide_generator_args=generator_args,
        generator_closure=generator_closure,
        halide_target=multitarget,
        halide_function_name=function_name,
        sanitizer=select({
            "@halide//:halide_config_msan": "msan",
            "//conditions:default": "",
        }),
        debug_codegen_level=debug_codegen_level,
        tags=["manual"] + tags,
        outputs=outputs)
    libname = "halide_internal_%s_%s" % (name, arch)
    if "static_library" in outputs:
      native.cc_library(
          name=libname,
          srcs=[":%s.a" % sub_name],
          tags=["manual"] + tags,
          visibility=["//visibility:private"])
    elif "cpp" in outputs:
      # TODO: yuck. hacky for apps/c_backend.
      if len(multitarget) > 1:
        fail(
            'can only request .cpp output if no multitargets are selected. Try' +
            ' adding halide_target_map={"*":["*"]} to your halide_library_from_generator ' +
            'rule.'
        )
      native.cc_library(
          name=libname,
          srcs=[":%s.cpp" % sub_name],
          tags=["manual"] + tags,
          visibility=["//visibility:private"])
    else:
      fail("either cpp or static_library required")
    condition_deps[_config_setting(
        base_target)] = _HALIDE_RUNTIME_OVERRIDES.get(
            base_target, []) + [":%s" % libname]

  # Note that we always build the .h file using the first entry in
  # the _HALIDE_TARGET_CONFIG_INFO table.
  header_target = _select_multitarget(
      base_target=_HALIDE_TARGET_CONFIG_INFO[0][0],
      halide_target_features=full_halide_target_features,
      halide_target_map=halide_target_map)
  if len(header_target) > 1:
    # This can happen if someone uses halide_target_map
    # to force everything to be multitarget. In that
    # case, just use the first entry.
    header_target = [header_target[0]]

  outputs = ["h"]
  _gengen(
      name="%s_header" % name,
      filename=name,
      halide_generator_args=generator_args,
      generator_closure=generator_closure,
      halide_target=header_target,
      halide_function_name=function_name,
      outputs=outputs,
      tags=tags)

  runtime_library = _halide_library_runtime_target_name(user_halide_target_features)
  if runtime_library in _standard_library_runtime_names():
    runtime_library = "@halide//:%s" % runtime_library
  else:
    if not native.existing_rule(runtime_library):
      _define_halide_library_runtime(user_halide_target_features)
      # Note to maintainers: if this message is reported, you probably want to add
      # feature combination as an item in _standard_library_runtime_features()
      # in this file. (Failing to do so will only cause potentially-redundant
      # runtime library building, but no correctness problems.)
      print("\nCreating Halide runtime library for feature set combination: " +
            str(_discard_useless_features(user_halide_target_features)) + "\n" +
            "If you see this message, there is no need to take any action; " +
            "however, please forward this message to halide-dev@lists.csail.mit.edu " +
            "so that we can include this case to reduce build times.")
    runtime_library = ":%s" % runtime_library

  native.cc_library(
      name=name,
      hdrs=[":%s_header" % name],
      # Order matters: runtime_library must come *after* condition_deps, so that
      # they will be presented to the linker in this order, and we want
      # unresolved symbols in the generated code (in condition_deps) to be
      # resolved in the runtime library.
      deps=select(condition_deps) + deps + ["@halide//:runtime", runtime_library],
      includes=includes,
      tags=tags,
      visibility=visibility)

  # Return the fully-qualified built target name.
  return "//%s:%s" % (PACKAGE_NAME, name)

def halide_library(name,
                   srcs,
                   copts=[],
                   debug_codegen_level=0,
                   extra_outputs=[],  # "stmt" and/or "assembly" are useful for debugging
                   filter_deps=[],
                   function_name=None,
                   generator_args="",
                   generator_deps=[],
                   generator_name=None,
                   halide_target_features=[],
                   halide_target_map=halide_library_default_target_map(),
                   includes=[],
                   namespace=None,
                   visibility=None):
  halide_generator(
      name="%s_generator" % name,
      srcs=srcs,
      generator_name=generator_name,
      deps=generator_deps,
      includes=includes,
      copts=copts,
      visibility=visibility)

  return halide_library_from_generator(
      name=name,
      generator=":%s_generator" % name,
      deps=filter_deps,
      visibility=visibility,
      namespace=namespace,
      includes=includes,
      function_name=function_name,
      generator_args=generator_args,
      debug_codegen_level=debug_codegen_level,
      halide_target_features=halide_target_features,
      halide_target_map=halide_target_map,
      extra_outputs=extra_outputs)

def halide_binary_to_cc_library(name, 
                                srcs, 
                                identifier, 
                                copts=None, 
                                visibility=None, 
                                alwayslink=False,
                                linkstatic=False):
  """Convenient wrapper around the binary2cpp tool to convert a single binary file to a cc_library."""
  if len(srcs) != 1:
    fail("halide_binary_to_cc_library requires exactly one source file.")

  native.genrule(
      name = "%s_cpp" % name,
      tools = [ "@halide//tools:binary2cpp" ],
      srcs = srcs,
      cmd = "$(location @halide//tools:binary2cpp) %s < $< > $@" % identifier,
      outs = [ "%s.cpp" % name ],
      visibility=["//visibility:private"]
  )
  native.genrule(
      name = "%s_h" % name,
      tools = [ "@halide//tools:binary2cpp" ],
      cmd = "$(location @halide//tools:binary2cpp) %s -header > $@" % identifier,
      outs = [ "%s.h" % name ],
      visibility=["//visibility:private"]
  )
  native.cc_library(
      name=name,
      srcs=[":%s_cpp" % name],
      hdrs=[":%s_h" % name],
      copts=copts,
      visibility=visibility,
      alwayslink=alwayslink,
      linkstatic=linkstatic
  )
