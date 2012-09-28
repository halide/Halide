#!/usr/bin/env python
# Requires Python 2.6 for the subprocess module

import subprocess
from os import chdir
from os.path import isfile, isdir
import sys

from util import status

from pbs import Command, make, git

import os

from multiprocessing import cpu_count
max_load = cpu_count() * 0.9

minimal = False
if '--minimal' in sys.argv:
    minimal = True

def check_llvm():
    try:
        llvm_path = './llvm/Release+Asserts'
        assert isfile(llvm_path+'/lib/ocaml/llvm.cma') # is llvm.cma there?
        llvm_config = Command(llvm_path+'/bin/llvm-config')
        ver = llvm_config('--version') # try calling llvm-config
        print ver
        assert '3.2' in ver 
        return True
    except:
        return False


# Test for ocaml 3.12.*
status('Testing for OCaml 3.12.* or greater')
from pbs import ocaml, ocamlbuild
ver = ocaml('-version')
print ver
assert '3.12' in ver or '4.' in ver
print '...OK!'

platform = sys.platform.lower()
if 'linux' in platform:
    try:
        status('Testing for g++')
        from pbs import which
        assert which('g++')
        print '...OK!'

        status('Testing for package libc6-dev-i386')
        assert isfile('/usr/include/x86_64-linux-gnu/gnu/stubs-32.h')
        print '...OK!'
    
        status('Testing for package libsexplib-camlp4-dev')
        assert isdir('/usr/lib/ocaml/sexplib')
        print '...OK!'
    except:
        print 'You appear to be missing some required packages. Try:'
        print 'sudo apt-get install g++ libc6-i386-dev ocaml libsexplib-camlp4-dev'
        sys.exit(1)

if 'darwin' in platform:
    status('Testing for a sufficiently new g++')
    gxx = Command('g++')
    ver = gxx('--version')
    try:
        assert 'g++-4.' in ver
        print '...OK!'
    except:
        print 'Your g++ compiler is missing or too old.'
        print 'Trying installing the command line tools from xcode.'
        print 'They can be found in preferences -> downloads -> command line tools.'
        print 'If that doesn\'t work, update xcode and try again.'
        sys.exit(1)

# Submodule update/init
# TODO: make --recursive optional
status('Checking out submodules')
git('submodule', 'update', '--init', '--recursive')

# TODO: always run make -C llvm, just to make sure it's up to date. Does configure cache its settings when a reconfigure is forced?
# TODO: make install in subdir, with docs
#        requires graphviz, doxygen; target ocamlbuild to alt dir?; make clean?
# Build llvm
if check_llvm():
    status('llvm appears to be present -- skipping')
else:
    chdir('llvm')
    configure = Command('./configure')
    llvm_cfg = ['--enable-assertions', '--enable-optimized']
    if minimal:
        llvm_cfg = llvm_cfg + ['--enable-targets=host,ptx,x86_64,arm']
    else:
        llvm_cfg = llvm_cfg + ['--enable-targets=all', '--enable-docs', '--enable-doxygen']
    status('''Configuring llvm:
    %s''' % llvm_cfg)
    print configure(llvm_cfg)
    
    status('Building llvm')
    # print make('-j', '--load-average=%f' % max_load)
    print make('-j12')

    chdir('docs')
    if not minimal:
        make('ocamldoc', '-j', '--load-average=%f' % max_load)
    # optional: check_call('make doxygen'.split(' '))
    chdir('../..')
    assert check_llvm()

# Test building 
chdir('src')
status('Test: building halide.cmxa')
print ocamlbuild('-use-ocamlfind', 'halide.cmxa')
chdir('..')

status('Building C++ bindings')
print make('-C', 'cpp_bindings', '-j1') # can be flakey with first parallel build on SSD
