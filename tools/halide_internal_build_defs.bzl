# Description:
#  Private Skylark helper functions for building Halide.
#  Should not be used by code outside of Halide itself.

def _gen_runtime_cpp_component_1(component_file, bits, suffix, opts) :
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
  native.genrule(
    name = "initmod.{0}_{1}{2}.ll".format(component_file, bits, suffix),
    tools = [ "@llvm//:clang" ],
    srcs = [ "src/runtime/{0}.cpp".format(component_file),
             ":runtime_files",
           ],
    cmd = ("$(location @llvm//:clang) " +
           "-ffreestanding -fno-blocks -fno-exceptions -fno-unwind-tables -m{1} " +
           "-target " + triple + " " +
           "-DCOMPILING_HALIDE_RUNTIME -DBITS_{1} -emit-llvm -S " +
           "$(location src/runtime/{0}.cpp) -o $@").format(component_file, bits, opts),
    outs = [ "initmod_{0}_{1}{2}.ll".format(component_file, bits, suffix), ]
  )

def _gen_runtime_cpp_component_2(component_file, bits, suffix = "") :
  native.genrule(
    name = "initmod.{0}_{1}{2}.bc".format(component_file, bits, suffix),
    tools = [ "@llvm//:llvm-as" ],
    srcs = [ "initmod.{0}_{1}{2}.ll".format(component_file, bits, suffix) ],
    cmd = "$(location @llvm//:llvm-as) $< -o $@",
    outs = [ "initmod_{0}_{1}{2}.bc".format(component_file, bits, suffix), ]
  )

def _gen_runtime_cpp_component_3(component_file, bits, suffix = "") :
  native.genrule(
    name = "initmod.{0}_{1}{2}.cpp".format(component_file, bits, suffix),
    tools = [ "@halide//tools:bitcode2cpp" ],
    srcs = [ "initmod.{0}_{1}{2}.bc".format(component_file, bits, suffix), ],
    cmd = "$(location @halide//tools:bitcode2cpp) {0}_{1}{2} < $< > $@".format(component_file, bits, suffix),
    outs = [ "initmod_{0}_{1}{2}.cpp".format(component_file, bits, suffix), ]
  )

def _gen_runtime_ll_component_1(component_file) :
  native.genrule(
    name = "initmod.{0}_ll.bc".format(component_file),
    tools = [ "@llvm//:llvm-as" ],
    srcs = [ "src/runtime/{0}.ll".format(component_file), ],
    cmd = "$(location @llvm//:llvm-as) $< -o $@",
    outs = [ "initmod_{0}_ll.bc".format(component_file), ]
  )

def _gen_runtime_ll_component_2(component_file) :
  native.genrule(
    name = "initmod.{0}_ll.cpp".format(component_file),
    tools = [ "@halide//tools:bitcode2cpp" ],
    srcs = [ "initmod.{0}_ll.bc".format(component_file), ],
    cmd = "$(location @halide//tools:bitcode2cpp) {0}_ll < $< > $@".format(component_file),
    outs = [ "initmod_{0}_ll.cpp".format(component_file) ]
  )

def _gen_runtime_nvidia_bitcode_component(component_file) :
  native.genrule(
    name = "initmod_ptx.{0}_ll.cpp".format(component_file),
    tools = [ "@halide//tools:bitcode2cpp" ],
    srcs = native.glob(["src/runtime/nvidia_libdevice_bitcode/libdevice.{0}.*.bc".format(component_file)], exclude = []),
    cmd = "$(location @halide//tools:bitcode2cpp) ptx_{0}_ll < $< > $@".format(component_file),
    outs = [ "initmod_ptx_{0}_ll.cpp".format(component_file) ]
  )

def gen_runtime_targets(runtime_cpp_components, runtime_ll_components, runtime_nvidia_bitcode_components):
  for component in runtime_cpp_components:
    for bits in [ "32", "64" ]:
      for suffix, opts in [ ("", "-O3"), ("_debug", "-g -DDEBUG_RUNTIME") ]:
        _gen_runtime_cpp_component_1(component, bits, suffix,  opts)
        _gen_runtime_cpp_component_2(component, bits, suffix)
        _gen_runtime_cpp_component_3(component, bits, suffix)
  for component in runtime_ll_components:
    _gen_runtime_ll_component_1(component)
    _gen_runtime_ll_component_2(component)
  for component in runtime_nvidia_bitcode_components:
    _gen_runtime_nvidia_bitcode_component(component)

def runtime_srcs(runtime_cpp_components, runtime_ll_components, runtime_nvidia_bitcode_components):
  result = []
  for component in runtime_cpp_components:
    for bits in [ "32", "64" ]:
      for suffix in [ "", "_debug" ]:
        result = result + [":initmod_{0}_{1}{2}.cpp".format(component, bits, suffix)]
  for component in runtime_ll_components:
    result = result + [":initmod_%s_ll.cpp" % component]
  for component in runtime_nvidia_bitcode_components:
    result = result + [":initmod_ptx_%s_ll.cpp" % component]
  return result


