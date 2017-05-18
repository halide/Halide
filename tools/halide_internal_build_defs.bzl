"""Private Skylark helper functions for building Halide."""
load(
    "@halide//:halide.bzl",
    "halide_binary_to_cc_library",
    "halide_language_copts",
)

def _ll2bc(name, srcs):
  if len(srcs) != 1:
    fail("_ll2bc requires exactly one src")
  native.genrule(
      name = "%s_ll2bc" % name,
      tools = [ "@llvm//:llvm-as" ],
      srcs = srcs,
      cmd = "$(location @llvm//:llvm-as) $< -o $@",
      outs = [ "%s.bc" % name ],
      visibility = [ "//visibility:private" ],
  )

def _binary2cpp(name, srcs):
  halide_binary_to_cc_library(
      name = "%s_b2clib" % name,
      srcs = srcs,
      copts = halide_language_copts(),
      identifier = "halide_internal_%s" % name,
      visibility = [ "//visibility:private" ],
  )
  return ":%s_b2clib" % name

def _gen_runtime_cpp_component(component_file, bits, suffix, opts) :
  # Pick the appropriate generic target triple
  if bits == "32":
    if component_file.startswith("windows_"):
      # windows runtime uses stdcall when compiled for 32-bit, which
      # is x86-specific.
      triple = "i386-unknown-unknown-unknown"
    else:
      # The 'nacl' is a red herring. This is just a generic 32-bit
      # little-endian target.
      triple = "le32-unknown-nacl-unknown"
  else:
    triple = "le64-unknown-unknown-unknown"
  name = "{0}_{1}{2}".format(component_file, bits, suffix)
  native.genrule(
      name = "initmod.{0}.ll".format(name),
      tools = [ "@llvm//:clang" ],
      srcs = [ 
        "src/runtime/{0}.cpp".format(component_file),
        ":runtime_files",
      ],
      cmd = ("$(location @llvm//:clang) " +
             "-target " + triple + " " +
             "-ffreestanding -fno-blocks -fno-exceptions -fno-unwind-tables -m{1} " +
             "-DCOMPILING_HALIDE_RUNTIME -DBITS_{1} -emit-llvm {2} -S " +
             "$(location src/runtime/{0}.cpp) -o $@").format(component_file, bits, opts),
      outs = [ "initmod_{0}.ll".format(name) ],
      visibility = [ "//visibility:private" ],
  )

  _ll2bc(name = "initmod_{0}".format(name),
         srcs = [ ":initmod.{0}.ll".format(name) ])

  return _binary2cpp(name = "initmod_{0}".format(name),
                     srcs = ["initmod_{0}.bc".format(name)])

def _gen_runtime_ll_component(component_file) :
  _ll2bc(name = "initmod_{0}_ll".format(component_file),
         srcs = [ "src/runtime/{0}.ll".format(component_file) ])

  return _binary2cpp(name = "initmod_{0}_ll".format(component_file),
                     srcs = ["initmod_{0}_ll.bc".format(component_file)])

def _gen_runtime_nvidia_bitcode_component(component_file) :
  return _binary2cpp(name = "initmod_ptx_{0}_ll".format(component_file),
                     srcs = native.glob(["src/runtime/nvidia_libdevice_bitcode/libdevice.{0}.*.bc".format(component_file)]))

def _gen_runtime_header_component(component_file) :
  return _binary2cpp(name = "runtime_header_{0}_h".format(component_file),
                     srcs = [ "src/runtime/{0}.h".format(component_file) ])

def _gen_inlined_c_component(srcs):
  return _binary2cpp(name = "initmod_inlined_c",
                     srcs = ["src/runtime/{0}.cpp".format(src) for src in srcs])

def gen_runtime(name,
                runtime_cpp_components, 
                runtime_ll_components, 
                runtime_nvidia_bitcode_components, 
                runtime_header_components, 
                runtime_inlined_c_components):
  """Build the Halide runtime libraries."""
  deps = [
      _gen_runtime_cpp_component(component, bits, suffix,  opts)
      for component in runtime_cpp_components
      for bits in [ "32", "64" ]
      for suffix, opts in [ ("", "-O3"), ("_debug", "-DDEBUG_RUNTIME") ]
  ] + [
      _gen_runtime_ll_component(component)
      for component in runtime_ll_components
  ] + [
      _gen_runtime_nvidia_bitcode_component(component)
      for component in runtime_nvidia_bitcode_components
  ] + [
      _gen_runtime_header_component(component)
      for component in runtime_header_components
  ] + [
      _gen_inlined_c_component(runtime_inlined_c_components)
  ]
  native.cc_library(name = name, 
                    deps = deps,
                    copts = halide_language_copts())



