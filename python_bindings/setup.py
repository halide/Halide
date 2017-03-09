#!/usr/bin/env python
"""

Install me with `pip install .`

Pulls in static libHalide.a from parent dir.
For example, from repo root:

    make -j
    cd python_bindings
    pip install .


Environment variables:

HALIDE_DIR: installation prefix of Halide, if needed (currently ignored)
BOOST_DIR: installation prefix of Boost, if needed
BOOST_PYTHON_LIB: name of the boost Python lib to link (e.g. boost_python3-mt)
    only necessary if boost_python cannot be found
"""

import glob
import os
from os.path import join
import sys

from distutils.ccompiler import new_compiler
from setuptools import setup
from setuptools.extension import Extension
from setuptools.command.bdist_egg import bdist_egg

import numpy

# setup include, lib dirs from prefixes
include_dirs = [numpy.get_include()]
library_dirs = []

prefixes = [sys.prefix, '/opt/local']
for prefix_env in ('HALIDE_DIR', 'BOOST_DIR'):
    prefix = os.getenv(prefix_env)
    if prefix and prefix not in prefixes:
        prefixes.insert(0, prefix)

for prefix in prefixes:
    if not os.path.isdir(prefix): continue
    include_dirs.append(join(prefix, 'include'))
    library_dirs.append(join(prefix, 'lib'))


def find_boost_python():
    """Figure out the name of boost_python.
    
    Could be: boost_python, boost_python3-mt, etc.
    """
    if os.getenv('BOOST_PYTHON_LIB'):
        return os.getenv('BOOST_PYTHON_LIB')
    base_name = 'boost_python'
    if sys.version_info[0] == 3:
        base_name += '3'
    cc = new_compiler()
    lib_dirs = [ os.path.join(p, 'lib') for p in prefixes if os.path.isdir(p) ]
    for name in [base_name + '-mt', base_name]:
        full_path = cc.find_library_file(lib_dirs, name)
        if full_path:
            return name
    raise ValueError("Couldn't find boost_python in %s" % os.pathsep.join(libs))

boost_python = find_boost_python()


here = os.path.abspath(os.path.dirname(__file__))
sources = glob.glob(join(here, 'python', '*.cpp')) + \
          glob.glob(join(here, 'numpy', '*.cpp'))


ext = Extension('halide', sources,
    include_dirs=include_dirs,
    library_dirs=library_dirs,
    extra_compile_args=['-std=c++11'],
    extra_link_args=[
        # get libHalide.a from the repo:
        # TODO: find it in PREFIX/lib
        os.path.join(here, '..', 'lib', 'libHalide.a'),
    ],
    libraries=['z', boost_python],
)


class bdist_egg_disabled(bdist_egg):
    """Disabled version of bdist_egg

    Prevents setup.py install performing setuptools' default easy_install,
    which it should never ever do.
    """
    def run(self):
        sys.exit("Aborting implicit building of eggs. Use `pip install .` to install from source.")



setup_args = dict(
    name='halide',
    version='2017.03.07', # TODO: some SemVer scheme should be introduced once this stabilizes
    ext_modules=[ext],
    cmdclass=dict(bdist_egg=bdist_egg_disabled),
    description='a language for image processing and computational photography',
    long_description='See http://halide-lang.org for more info.',
    author='Halide Contributors',
    author_email='halide-dev@lists.csail.mit.edu',
    url='http://halide-lang.org',
    license='MIT',
    classifiers = [
        'License :: OSI Approved :: MIT License',
        'Programming Language :: Python',
        'Programming Language :: Python :: 3',
    ],
)


if __name__ == '__main__':
    setup(**setup_args)

