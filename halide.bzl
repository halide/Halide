def halide_language_copts():
  # TODO: this is wrong for (e.g.) Windows and will need further specialization.
  return [
    "$(STACK_FRAME_UNLIMITED)",
    "-fno-rtti",
    "-fPIC",
    "-fvisibility-inlines-hidden",
    "-std=c++11",
    "-DGOOGLE_PROTOBUF_NO_RTTI"
  ]


def halide_language_linkopts():
  _linux_opts = ["-rdynamic"]
  _osx_opts = ["-Wl,-stack_size", "-Wl,1000000"]
  return select({
      "@halide//:halide_host_config_darwin": _osx_opts,
      "@halide//:halide_host_config_darwin_x86_64": _osx_opts,
      # TODO: this is wrong for (e.g.) Windows and will need further specialization.
      "//conditions:default": _linux_opts,
  })


def halide_runtime_linkopts():
  # TODO: this is wrong for (e.g.) Windows and will need further specialization.
  return [
    "-lpthread",
  ]


def halide_opengl_linkopts():
  _linux_opts = ["-lGL", "-lX11"]
  _osx_opts = ["-framework OpenGL"]
  return select({
      "@halide//:halide_platform_config_darwin": _osx_opts,
      "@halide//:halide_platform_config_darwin_x86_64": _osx_opts,
      # TODO: this is wrong for (e.g.) Windows and will need further specialization.
      "//conditions:default": _linux_opts,
  })


# (halide-target-base,  cpu,   android-cpu,   ios-cpu)
_HALIDE_TARGET_CONFIG_INFO = [
  # Android
  ("arm-32-android",    None,  "armeabi-v7a", None),
  ("arm-64-android",    None,  "arm64-v8a",   None),
  ("x86-32-android",    None,  "x86",         None),
  ("x86-64-android",    None,  "x86_64",      None),
  # iOS    
  ("arm-32-ios",        None,  None,          "armv7"),
  ("arm-64-ios",        None,  None,          "arm64"),
  # OSX    
  ("x86-32-osx",        None,  None,          "x86_32"),
  ("x86-64-osx",        None,  None,          "x86_64"),
  # Linux 
  ("arm-64-linux",     "arm",  None,          None),
  ("powerpc-64-linux", "ppc",  None,          None),
  ("x86-64-linux",     "k8",   None,          None),
  ("x86-32-linux",     "piii", None,          None),
  # TODO: add conditions appropriate for other targets/cpus: Windows, etc.
]


_HALIDE_TARGET_MAP_DEFAULT = {
  "x86-64-osx": [
    "x86-64-osx-sse41-avx-avx2-fma",
    "x86-64-osx-sse41-avx",
    "x86-64-osx-sse41",
    "x86-64-osx",
  ],
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
  "x86-64-nacl": [
    "x86-64-nacl-sse41",
    "x86-64-nacl"
  ],
  "x86-32-nacl": [
    "x86-32-nacl-sse41",
    "x86-32-nacl"
  ],
}

def _halide_host_config_settings():
  # TODO: this is incomplete for (e.g.) Windows and will need further specialization.
  _host_cpus = [
    "darwin",
    "darwin_x86_64",
  ]
  for host_cpu in _host_cpus:
    native.config_setting(
      name="halide_host_config_%s" % host_cpu,
      values={"host_cpu" : host_cpu},
      visibility=["//visibility:public"]
    )
    # TODO hokey, improve, this isn't really right in general
    native.config_setting(
        name = "halide_platform_config_%s" % host_cpu,
        values = {
            # "crosstool_top": "//tools/osx/crosstool",
            "cpu": host_cpu,
        },
        visibility=["//visibility:public"]
    )



def halide_config_settings():
  """Define the config_settings used internally by these build rules."""
  _halide_host_config_settings()
  for base_target, cpu, android_cpu, ios_cpu in _HALIDE_TARGET_CONFIG_INFO:
    if android_cpu == None:
      android_cpu = "armeabi"
    if ios_cpu == None:
      ios_cpu = "x86_64"
    if cpu != None:
      values={
        "cpu": cpu,
        "android_cpu": android_cpu,
        "ios_cpu": ios_cpu,
      }
    else:
      values={
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


def _halide_target_to_bazel_rule_name(multitarget):
  subtargets = multitarget.split(",")
  subtargets = [_canonicalize_target(st).replace("-", "_") for st in subtargets]
  return "_".join(subtargets)

def _config_setting_name(halide_target):
  """Take a Halide target string and converts to a unique name suitable for
   a Bazel config_setting."""
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
      for h in halide_target:
        new_outputs[o+h] = "%s_%s.%s" % (filename, _canonicalize_target(h).replace("-", "_"), ext)
    else:
      new_outputs[o] = "%s.%s" % (filename, ext)
  return new_outputs

def _gengen_impl(ctx):
  if _has_dupes(ctx.attr.outputs):
    fail("Duplicate values in outputs: " + str(ctx.attr.outputs))

  remaps = [".s=.s.txt"]
  halide_target = ctx.attr.halide_target
  if ctx.attr.sanitizer:
    halide_target = []
    for t in ctx.attr.halide_target:
      ct = _canonicalize_target("%s-%s" % (t, ctx.attr.sanitizer))
      halide_target += [ct]
      remaps += ["%s=%s" % (ct.replace("-", "_"), t.replace("-", "_"))]

  outputs = [ctx.new_file(f)
         for f in _gengen_outputs(ctx.attr.filename,
                    ctx.attr.halide_target,  # *not* halide_target
                    ctx.attr.outputs).values()]

  output_dir = outputs[0].dirname 
  arguments = ["-o", output_dir]
  if ctx.attr.filename:
    arguments += ["-n", ctx.attr.filename]
  if ctx.attr.halide_function_name:
    arguments += ["-f", ctx.attr.halide_function_name]
  if ctx.attr.halide_generator_name:
    arguments += ["-g", ctx.attr.halide_generator_name]
  if len(ctx.attr.outputs) > 0:
    arguments += ["-e", ",".join(ctx.attr.outputs)]
    arguments += ["-x", ",".join(remaps)]
  arguments += ["target=%s" % ",".join(halide_target)]
  if ctx.attr.halide_generator_args:
    arguments += ctx.attr.halide_generator_args.split(" ")

  resolved_inputs, _, input_manifests = ctx.resolve_command()
  env = {
    "HL_DEBUG_CODEGEN" : str(ctx.attr.debug_codegen_level),
    "HL_TRACE" : str(ctx.attr.trace_level),
  }
  n = ctx.attr.halide_generator_name if ctx.attr.halide_generator_name else "(default)"
  ctx.action(
    # If you need to force the tools to run locally (e.g. for experimentation),
    # uncommentthis line.
    # execution_requirements={"local":"1"},
    arguments=arguments,
    env=env,
    executable=ctx.executable.generator_binary,
    input_manifests=input_manifests,
    inputs=resolved_inputs,
    mnemonic="ExecuteHalideGenerator",
    outputs=outputs,
    progress_message="Executing generator %s with args (%s)..." % (n, ctx.attr.halide_generator_args),
  )


_gengen = rule(
  implementation=_gengen_impl,
  attrs={
    "debug_codegen_level": attr.int(),
    "filename": attr.string(),
    "generator_binary": attr.label(executable=True,
                     allow_files=True,
                     mandatory=True,
                     cfg="host"),
    "halide_target": attr.string_list(),
    "halide_function_name": attr.string(),
    "halide_generator_name": attr.string(),
    "halide_generator_args": attr.string(),
    "outputs": attr.string_list(),
    "sanitizer": attr.string(),
    "trace_level": attr.int(),
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
      fail("target %s does not start with expected target %s for halide_target_map" % (t, expected_base))
    t = t[len(expected_base):]
    if t.startswith("-"):
      t = t[1:]
    # Check for a "match all base targets" entry:
    multitarget.append(_add_target_features(base_target, t.split("-")))
  
  # Add the extra features (if any).
  if halide_target_features:
    multitarget = [_add_target_features(t, halide_target_features) for t in multitarget]
  
  # Finally, canonicalize all targets
  multitarget = [_canonicalize_target(t) for t in multitarget]
  return multitarget



def halide_library(name,
                   srcs,
                   hdrs=[],
                   filter_deps=[],
                   generator_deps=[],
                   visibility=None,
                   namespace=None,
                   function_name=None,
                   generator_name="",
                   generator_args="",
                   debug_codegen_level=0,
                   trace_level=0,
                   halide_target_features=[],
                   halide_target_map=_HALIDE_TARGET_MAP_DEFAULT,
                   extra_outputs=[],
                   includes=[]):

  if not function_name:
    function_name = name

  if namespace:
    function_name = "%s::%s" % (namespace, function_name)
    halide_target_features += ["c_plus_plus_name_mangling"]

  # Escape backslashes and double quotes.
  generator_args = generator_args.replace("\\", '\\\\"').replace('"', '\\"')

  if _has_dupes(halide_target_features):
    fail("Duplicate values in halide_target_features: %s" % str(halide_target_features))
  if _has_dupes(extra_outputs):
    fail("Duplicate values in extra_outputs: %s" % str(extra_outputs))

  outputs=extra_outputs
  # TODO: yuck. hacky for apps/c_backend.
  if not "cpp" in outputs:
    outputs += ["static_library"]

  generator_binary_name = "%s_generator_binary" % name
  native.cc_binary(
    name=generator_binary_name,
    srcs=srcs,
    copts=halide_language_copts(),
    linkopts=halide_language_linkopts(),
    deps=["@halide//:internal_halide_generator_glue"] + generator_deps,
    visibility=["//visibility:private"],
    tags=["manual"]
  )

  condition_deps = {}
  for base_target, _, _, _ in _HALIDE_TARGET_CONFIG_INFO:
    multitarget = _select_multitarget(
      base_target=base_target,
      halide_target_features=halide_target_features,
      halide_target_map=halide_target_map,
    )
    arch = _halide_target_to_bazel_rule_name(base_target)
    sub_name = "%s_%s" % (name, arch)
    _gengen(
      name=sub_name,
      filename=sub_name,
      halide_generator_name=generator_name,
      halide_generator_args=generator_args,
      generator_binary=generator_binary_name,
      halide_target=multitarget,
      halide_function_name=function_name,
      sanitizer = select({
        "@halide//:halide_config_msan": "msan",
        "//conditions:default": "",
      }),
      debug_codegen_level=debug_codegen_level,
      trace_level=trace_level,
      outputs=outputs
    )
    libname = "halide_internal_%s_%s" % (name, arch)
    if "static_library" in outputs:
      native.cc_library(name=libname,
                srcs=[":%s.a" % sub_name],
                visibility=["//visibility:private"])
    elif "cpp" in outputs:
      # TODO: yuck. hacky for apps/c_backend.
      if len(multitarget) > 1:
        fail('can only request .cpp output if no multitargets are selected. Try adding halide_target_map={"*":["*"]} to your halide_library rule.')
      native.cc_library(name=libname,
                srcs=[":%s.cpp" % sub_name],
                visibility=["//visibility:private"])
    else:
      fail("either cpp or static_library required")
    condition_deps[_config_setting(base_target)] = [":%s" % libname]

  # Note that we always build the .h file using the first entry in
  # the _HALIDE_TARGET_CONFIG_INFO table.
  header_target = _select_multitarget(
    base_target=_HALIDE_TARGET_CONFIG_INFO[0][0],
    halide_target_features=halide_target_features,
    halide_target_map=halide_target_map
  )
  if len(header_target) > 1:
    # This can happen if someone uses halide_target_map
    # to force everything to be multitarget. In that
    # case, just use the first entry.
    header_target = [header_target[0]]
  _gengen(
    name="%s_header" % name,
    filename=name,
    halide_generator_name=generator_name,
    halide_generator_args=generator_args,
    generator_binary=generator_binary_name,
    halide_target=header_target,
    halide_function_name=function_name,
    outputs=["h"]
  )

  native.cc_library(
    name=name,
    hdrs=[":%s_header" % name] + hdrs,
    deps=["@halide//:runtime"] + select(condition_deps) + filter_deps,
    includes=includes,
    visibility=visibility
  )
