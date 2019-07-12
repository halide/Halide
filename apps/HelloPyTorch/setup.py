import os
import platform
import re
from setuptools import setup, find_packages

from torch.utils.cpp_extension import BuildExtension
import torch as th


def generate_pybind_wrapper(path, headers, has_cuda):
  s = "#include \"torch/extension.h\"\n\n"
  if has_cuda:
    s += "#define HL_PT_CUDA\n"
  s += "#include \"HalidePyTorchHelpers.h\"\n"
  for h in headers:
    s += "#include \"{}\"\n".format(os.path.splitext(h)[0]+".pytorch.h")
  if has_cuda:
    s += "#undef HL_PT_CUDA\n"

  s += "\nPYBIND11_MODULE(TORCH_EXTENSION_NAME, m) {\n"
  for h in headers:
    name = os.path.splitext(h)[0]
    s += "  m.def(\"{}\", &{}_th_, \"PyTorch wrapper of the Halide pipeline {}\");\n".format(
      name, name, name)
  s += "}\n"
  with open(path, 'w') as fid:
    fid.write(s)

if __name__ == "__main__":
  # This is where the generate Halide ops headers live. We also generate the .cpp
  # wrapper in this directory
  build_dir = os.getenv("BIN")
  if build_dir is None or not os.path.exists(build_dir):
    raise ValueError("Bin directory {} is invalid".format(build_dir))

  # Path to a distribution of Halide
  halide_dir = os.getenv("HALIDE_DISTRIB_PATH")
  if halide_dir is None or not os.path.exists(halide_dir):
    raise ValueError("Halide directory {} is invalid".format(halide_dir))

  has_cuda = os.getenv("HAS_CUDA")
  if has_cuda is None or has_cuda == "0":
    has_cuda = False
  else:
    has_cuda = True

  include_dirs = [build_dir, os.path.join(halide_dir, "include")]
  compile_args = ["-std=c++11", "-g"]
  if platform.system() == "Darwin":  # on osx libstdc++ causes trouble
    compile_args += ["-stdlib=libc++"]  

  re_cc = re.compile(r".*\.pytorch\.h")
  hl_srcs = [f for f in os.listdir(build_dir) if re_cc.match(f)]

  ext_name = "halide_ops"
  hl_libs = []  # Halide op libraries to link to
  hl_headers = []  # Halide op headers to include in the wrapper
  for f in hl_srcs:
    # Add all Halide generated torch wrapper
    hl_src = os.path.join(build_dir, f)

    # Add all Halide-generated libraries
    hl_lib = hl_src.split(".")[0] + ".a"
    hl_libs.append(hl_lib)

    hl_header = hl_src.split(".")[0] + ".h"
    hl_headers.append(os.path.basename(hl_header))

  # C++ wrapper code that includes so that we get all the Halide ops in a
  # single python extension
  wrapper_path = os.path.join(build_dir, "pybind_wrapper.cpp")
  sources = [wrapper_path]

  if has_cuda:
    print("Generating CUDA wrapper")
    generate_pybind_wrapper(wrapper_path, hl_headers, True)
    from torch.utils.cpp_extension import CUDAExtension
    extension = CUDAExtension(ext_name, sources,
                              extra_objects=hl_libs,
                              libraries=["cuda"],  # Halide ops need the full cuda lib, not just the RT library
                              extra_compile_args=compile_args)
  else:
    print("Generating CPU wrapper")
    generate_pybind_wrapper(wrapper_path, hl_headers, False)
    from torch.utils.cpp_extension import CppExtension
    extension = CppExtension(ext_name, sources,
                             extra_objects=hl_libs,
                             extra_compile_args=compile_args)

  # Build the Python extension module
  setup(name=ext_name,
        verbose=True,
        url="",
        author_email="your@email.com",
        author="Some Author",
        version="0.0.0",
        ext_modules=[extension],
        include_dirs=include_dirs,
        cmdclass={"build_ext": BuildExtension}
        )
