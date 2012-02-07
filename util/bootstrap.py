#!/usr/bin/env python
# Requires Python 2.6 for the subprocess module

import subprocess
from os import chdir
from os.path import isfile
import sys

from util import status

from pbs import Command, make, git

import os

def check_llvm():
    try:
        llvm_path = './llvm/Release+Asserts'
        assert isfile(llvm_path+'/lib/ocaml/llvm.cma') # is llvm.cma there?
        llvm_config = Command(llvm_path+'/bin/llvm-config')
        ver = llvm_config('--version') # try calling llvm-config
        print ver
        assert '3.1' in ver
        return True
    except:
        return False


# Test for ocaml 3.12.*
status('Testing for OCaml 3.12.*')
from pbs import ocaml, ocamlbuild
ver = ocaml('-version')
print ver
assert '3.12' in ver
print '...OK!'

# Submodule update/init
# TODO: make --recursive optional
status('Checking out submodules')
git('submodule', 'update', '--init', '--recursive')

# TODO: make install in subdir, with docs
#        requires graphviz, doxygen; target ocamlbuild to alt dir?; make clean?
# Build llvm
if check_llvm():
    status('llvm appears to be present -- skipping')
else:
    chdir('llvm')
    configure = Command('./configure')
    llvm_cfg = ['--enable-assertions', '--enable-optimized', '--enable-targets=all']#, '--enable-docs', '--enable-doxygen']
    status('''Configuring llvm:
    %s''' % cfg_str)
    print configure(llvm_cfg)
    
    status('Building llvm')
    print make('-j12')
    
    chdir('docs')
    #check_call('make ocamldoc'.split(' '))
    # optional: check_call('make doxygen'.split(' '))
    chdir('../..')
    assert check_llvm()

# Test building 
chdir('src')
status('Test: building halide.cmxa')
print ocamlbuild('halide.cmxa')
chdir('..')

status('Building C++ bindings')
print make('-C', 'cpp_bindings')
