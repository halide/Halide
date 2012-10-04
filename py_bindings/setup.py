from distutils.core import setup
from distutils.extension import Extension
#from Cython.Distutils import build_ext
import os, os.path

import subprocess

png_cflags  = subprocess.check_output('libpng-config --cflags',  shell=True).strip()
png_ldflags = subprocess.check_output('libpng-config --ldflags', shell=True).strip()

ext_modules = [Extension("_cHalide", ["cHalide_wrap.cxx", 'py_util.cpp', 'environ_fix.cpp'],
                         include_dirs=['../cpp_bindings', '/opt/local/include/libpng14'],
                         extra_compile_args=('-ffast-math -O3 -msse -Wl,-dead_strip -fno-common' + ' ' + png_cflags).split(),
                         #libraries=['Halide.a'],
                         library_dirs=['/opt/local/lib'],
                         ##extra_link_args=[], #['../cpp_bindings/Halide.a'],
                         extra_link_args=['../cpp_bindings/libHalide.a', '-lpthread', '-ldl', '-lstdc++', '-lc']+png_ldflags.split(),
                         language='c++')]

for (infile, outfile) in [('apollo2.jpg', 'apollo2.png')]:
  if not os.path.exists(outfile):
    os.system('convert %s %s' % (infile, outfile))
    if not os.path.exists(outfile):
      try:
        import Image
        Image.open(infile).save(outfile)
        assert os.path.exists(outfile)
      except:
        raise ValueError('Could not convert (via ImageMagick or Python Image library) %s => %s' % (infile, outfile))

setup(
  name = 'Halide binding',
#  cmdclass = {'build_ext': build_ext},
  ext_modules = ext_modules
)
