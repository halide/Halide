from distutils.core import setup
from distutils.extension import Extension
import os, os.path, sys
import glob

import subprocess
import shutil

png_cflags  = subprocess.check_output('libpng-config --cflags',  shell=True).strip()
png_ldflags = subprocess.check_output('libpng-config --ldflags', shell=True).strip()

ext_modules = [Extension("halide/_cHalide", ["halide/cHalide_wrap.cxx", 'halide/py_util.cpp'],
                         include_dirs=['../include'],
                         extra_compile_args=('-ffast-math -O3 -msse -Wl,-dead_strip -fno-common' + ' ' + png_cflags).split(),
                         extra_link_args=['../bin/libHalide.a', '-lpthread', '-ldl', '-lstdc++', '-lc']+png_ldflags.split(),
                         language='c++')]

if glob.glob('halide/data/*.png') == []:
    shutil.copytree('../apps/images/', 'halide/data')
    
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

