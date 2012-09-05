from distutils.core import setup
from distutils.extension import Extension
from Cython.Distutils import build_ext

import os

ext_modules = [Extension("_cHalide", ["cHalide_wrap.cxx", 'py_util.cpp', 'environ_fix.cpp'],
                         include_dirs=['../cpp_bindings', '/opt/local/include/libpng14'],
                         extra_compile_args=['-std=c++0x'] + '-ffast-math -O3 -msse -Wl,-dead_strip -fno-common'.split(),
                         #libraries=['Halide.a'],
                         library_dirs=['/opt/local/lib'],
                         ##extra_link_args=[], #['../cpp_bindings/Halide.a'],
                         extra_link_args=['../cpp_bindings/libHalide.a', '-lpthread', '-ldl', '-lpng', '-lpng14', '-lglib-2.0', '-lgio-2.0', '-lt1', '-lstdc++', '-lc'],
                         language='c++')]

setup(
  name = 'Halide binding',
  cmdclass = {'build_ext': build_ext},
  ext_modules = ext_modules
)
