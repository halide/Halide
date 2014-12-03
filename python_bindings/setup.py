from distutils.core import setup
from distutils.extension import Extension
import os, os.path, sys
import glob

import subprocess
import shutil

build_prefix = os.getenv('BUILD_PREFIX')
if not build_prefix:
    build_prefix = ''
halide_root = '..'
include_path = os.path.join(halide_root, 'include')
bin_path = os.path.join(halide_root, 'bin', build_prefix)
image_path = os.path.join(halide_root, 'apps', 'images')

png_cflags  = subprocess.check_output('libpng-config --cflags',  shell=True).strip().decode()
png_ldflags = subprocess.check_output('libpng-config --ldflags', shell=True).strip().decode()

ext_modules = [Extension("halide/_cHalide", ["halide/cHalide_wrap.cxx", 'halide/py_util.cpp'],
                         include_dirs=[include_path],
                         extra_compile_args=('-ffast-math -O3 -msse -Wl,-dead_strip -fno-common' + ' ' + png_cflags).split(),
                         extra_link_args=[os.path.join(bin_path, 'libHalide.a'),
                                          '-ltinfo', '-lpthread',
                                          '-ldl', '-lstdc++', '-lc'] + png_ldflags.split(),
                         language='c++')]

if glob.glob('halide/data/*.png') == []:
    shutil.copytree(image_path, 'halide/data')
    
setup(
    name = 'halide',
    version = '0.2',
    author = 'Connelly Barnes',
    license = 'MIT',
    classifiers=[
        "Topic :: Multimedia :: Graphics",
        "Programming Language :: Python :: 2.7"],
    packages=['halide'],
    package_dir={'halide': 'halide'},
    package_data={'halide': ['data/*.png']},
    ext_modules = ext_modules
)

