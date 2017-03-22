#!/usr/bin/env python
"""

Install me with `pip install .`

Pulls in static libHalide.a from parent dir.
For example, from repo root:

    make -j
    cd python_bindings
    NPY_NUM_BUILD_JOBS=16 HALIDE_DIR=$PWD/.. pip install .
    # or to build a wheel for distribution:
    python setup.py bdist_wheel


Environment variables:

HALIDE_DIR: installation prefix of Halide, if needed
BOOST_DIR: installation prefix of Boost, if needed
BOOST_PYTHON_LIB: name of the boost Python lib to link (e.g. boost_python3-mt)
    only necessary if boost_python cannot be found
"""

import glob
import os
from os.path import join
import sys
from subprocess import Popen, PIPE

# patch exec_command to allow parallel builds on mac
# (numpy bug, should be fixed in numpy 1.13)
from numpy.distutils import exec_command, ccompiler
def patch_exec_command(command, execute_in='', use_shell=None, use_tee=None,
                 _with_python = 1, **env ):
    execute_in = os.path.abspath(execute_in)

    if isinstance(command, list) and use_shell:
        # need string with shell=True
        command = ' '.join(pipes.quote(arg) for arg in command)
    elif not isinstance(command, list) and not use_shell:
        # need list with shell=False
        command = shlex.split(command)
    cmd_env = dict(os.environ)
    cmd_env.update(env)
    p = Popen(command, stdout=PIPE, shell=use_shell, env=cmd_env, cwd=execute_in)
    out, _ = p.communicate()
    if sys.version_info[0] >= 3:
        out = out.decode(sys.getdefaultencoding(), 'replace')
    return p.returncode, out

exec_command.exec_command = ccompiler.exec_command = patch_exec_command


from setuptools import setup
from numpy.distutils.extension import Extension
from setuptools.command.bdist_egg import bdist_egg

from numpy.distutils.extension import Extension

import numpy

# setup include, lib dirs from prefixes
include_dirs = [numpy.get_include()]
library_dirs = []

# NOTE: /opt/local is included to mirror the Makefile behavior, but should 
#       probably be removed eventually. No reason to treat it specially.
here = os.path.abspath(os.path.dirname(__file__))
prefixes = [join(here, '..'), sys.prefix, '/opt/local']
for prefix_env in ('HALIDE_DIR', 'BOOST_DIR'):
    prefix = os.getenv(prefix_env)
    if prefix and prefix not in prefixes:
        prefixes.insert(0, prefix)

for prefix in prefixes:
    if not os.path.isdir(prefix): continue
    include_dirs.append(join(prefix, 'include'))
    library_dirs.append(join(prefix, 'lib'))

cc = ccompiler.new_compiler()


def find_static_lib(names, env_key):
    """Find a static library

    Returns abspath if found, raises IOError if not.
    """
    
    if not isinstance(names, list):
        names = [names]
    
    env_prefix = os.getenv(env_key)
    if env_prefix:
        search = [env_prefix]
    else:
        search = prefixes + ['/usr/local', '/usr']
    lib_dirs = [ join(p, 'lib') for p in search if os.path.isdir(p) ]
    lib_names = [ cc.library_filename(name) for name in names ]
    for lib_dir in lib_dirs:
        for lib_name in lib_names:
            lib = join(lib_dir, lib_name)
            if os.path.exists(lib):
                return lib
    raise IOError("Couldn't find %s in %s. You may need to set %s" % (
        ' or '.join(lib_names), os.pathsep.join(lib_dirs), env_key
    ))


def find_boost_python():
    """Figure out the name of boost_python.
    
    Could be: boost_python, boost_python3-mt, etc.
    """
    name_specified = os.getenv('BOOST_PYTHON_LIB')
    if name_specified:
        names = [name_specified]
    else:
        base_name = 'boost_python'
        if sys.version_info[0] == 3:
            base_name += '3'
        names = [base_name + '-mt', base_name, 'boost_python-py%i%i' % sys.version_info[:2]]
    
    print("\nSearching for Boost Python")
    for name in names:
        if cc.has_function('rand',
                            includes=['stdlib.h'],
                            library_dirs=library_dirs,
                            libraries=[name]):
            print("Found Boost Python: %s" % name)
            return name
    else:
        raise IOError("Failed to find boost_python in %s."
        " Maybe set BOOST_DIR and/or BOOST_PYTHON_LIB" % os.pathsep.join(library_dirs))
    return find_static_lib(names, 'BOOST_DIR')


boost_python = find_boost_python()
libHalide = find_static_lib('Halide', 'HALIDE_DIR')


sources = glob.glob(join(here, 'python', '*.cpp')) + \
          glob.glob(join(here, 'numpy', '*.cpp'))


ext = Extension('halide', sources,
    include_dirs=include_dirs,
    library_dirs=library_dirs,
    extra_compile_args=['-std=c++11'],
    extra_objects=[libHalide],
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

