from distutils.core import setup
from distutils.extension import Extension
#from Cython.Distutils import build_ext
import os, os.path, sys

import subprocess

png_cflags  = subprocess.check_output('libpng-config --cflags',  shell=True).strip()
png_ldflags = subprocess.check_output('libpng-config --ldflags', shell=True).strip()

ext_modules = [Extension("_cHalide", ["cHalide_wrap.cxx", 'py_util.cpp'],
                         include_dirs=['../include'], #, '/opt/local/include/libpng14'],
                         extra_compile_args=('-ffast-math -O3 -msse -Wl,-dead_strip -fno-common' + ' ' + png_cflags).split(),
                         #libraries=['Halide.a'],
                         #library_dirs=['/opt/local/lib'],
                         ##extra_link_args=[], #['../cpp_bindings/Halide.a'],
                         extra_link_args=['../bin/libHalide.a', '-lpthread', '-ldl', '-lstdc++', '-lc']+png_ldflags.split(),
                         language='c++')]
    
setup(
  name = 'Halide binding',
#  cmdclass = {'build_ext': build_ext},
  ext_modules = ext_modules
)

