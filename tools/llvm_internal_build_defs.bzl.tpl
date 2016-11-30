_components_map = {
  "aarch64" : "WITH_AARCH64",
  "arm" : "WITH_ARM",
  "hexagon" : "WITH_HEXAGON",
  "mips" : "WITH_MIPS",
  "nacltransforms" : "WITH_NATIVE_CLIENT",
  "nvptx" : "WITH_PTX",
  "powerpc" : "WITH_POWERPC",
  "x86" : "WITH_X86",
}

def get_llvm_executable_extension():
  return %{llvm_executable_extension}

def get_llvm_version():
  return %{llvm_version}

def get_llvm_enabled_components():
  flags = []
  for c in %{llvm_components}:
    if c in _components_map:
      flags.append(_components_map[c])
  return flags

def get_llvm_copts():
  return %{llvm_cxxflags}

def get_llvm_linkopts():
  return %{llvm_ldflags} + %{llvm_libs} + %{llvm_system_libs}

def get_llvm_static_libs():
  return ["lib/%s" % lib for lib in %{llvm_static_libs}]

