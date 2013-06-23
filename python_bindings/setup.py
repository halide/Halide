from distutils.core import setup
from distutils.extension import Extension
import os, os.path, sys
import glob

import subprocess

png_cflags  = subprocess.check_output('libpng-config --cflags',  shell=True).strip()
png_ldflags = subprocess.check_output('libpng-config --ldflags', shell=True).strip()

ext_modules = [Extension("_cHalide", ["cHalide_wrap.cxx", 'py_util.cpp'],
                         include_dirs=['../include'],
                         extra_compile_args=('-ffast-math -O3 -msse -Wl,-dead_strip -fno-common' + ' ' + png_cflags).split(),
                         extra_link_args=['../bin/libHalide.a', '-lpthread', '-ldl', '-lstdc++', '-lc']+png_ldflags.split(),
                         language='c++')]

#if not os.path.exists('halide/data') or glob.glob
setup(
    name = 'halide',
    version = '0.1',
    author = 'Connelly Barnes',
    license = 'MIT',
    classifiers=[
        "Topic :: Multimedia :: Graphics",
        "Programming Language :: Python :: 2.7"],
    #packages=['halide']
    #package_dir={}
    package_data={'halide': ['../apps/images/*.png']},
    #data_files=[('halide_images', glob.glob('../apps/images/*.png'))],
    py_modules=['halide', 'cHalide'],
    ext_modules = ext_modules
)

