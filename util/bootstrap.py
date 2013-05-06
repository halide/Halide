#!/usr/bin/env python

from os import chdir
from os.path import isfile, isdir
import sys

from util import status

from sh import Command, make, git

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

class OCamlVersionError(Exception):
    url = 'http://caml.inria.fr/'
    def __str__(self):
        return 'OCaml >= 3.12 is required'

class OCamlFindlibError(Exception):
    url = 'http://projects.camlcity.org/projects/findlib.html'
    def __str__(self):
        return 'OCaml findlib is required'

class OCamlLibMissingError(Exception):
    def __init__(self, name):
        self.name = name
    def __str__(self):
        return 'Missing required OCaml library: %s' % self.name

def check_ocaml():
    try:
        # Test for ocaml 3.12.*
        status('Testing for OCaml 3.12.* or greater')
        from sh import ocaml, ocamlbuild
        ver = ocaml('-version')
        print ver
        assert '3.12' in ver or '4.' in ver
    except:
        raise OCamlVersionError()
    
    try:
        status('Testing for ocamlfind')
        from sh import ocamlfind
        print '..OK!'
    except:
        raise OCamlFindlibError()
    
    def assert_lib_version(lib, ver_req):
        def lib_version(lib):
            status('Testing for %s' % lib)
            info = ocamlfind('query', '-l', lib)
            return [l.split()[-1] for l in info.splitlines() if l.startswith('version')][0]
        try:
            ver = lib_version(lib)
            ver = float( '.'.join( ver.split('.')[0:2] ) )
            assert ver >= ver_req
        except:
            raise OCamlLibMissingError('%s >= %.1f' % (lib, ver_req))
    
    assert_lib_version('sexplib', 7.0)
    # We don't actually use batteries right now
    #assert_lib_version('batteries', 1.4)
    return ocamlbuild

ocamlbuild = None
platform = sys.platform.lower()
if 'linux' in platform:
    try:
        status('Testing for g++')
        from sh import which
        assert which('g++')
        print '...OK!'

        status('Testing for package libc6-dev-i386')
        assert isfile('/usr/include/x86_64-linux-gnu/gnu/stubs-32.h')
        print '...OK!'
        
        try:
            ocamlbuild = check_ocaml()
            print '...OK!'
        except OCamlVersionError as e:
            print 'e'
    
        status('Testing for clang')
        assert which('clang')
        print '...OK!'
            
    except:
        print 'You appear to be missing some required packages. Try:'
        print 'sudo apt-get install clang g++ libc6-dev-i386 ocaml libsexplib-camlp4-dev'
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
    try:
        ocamlbuild = check_ocaml()
        print '...OK!'
    except OCamlVersionError as e:
        print e
        sys.exit(1)
    except OCamlFindlibError as e:
        print e
        sys.exit(1)
    except OCamlLibMissingError as e:
        print e
        sys.exit(1)

# Submodule update/init
# TODO: make --recursive optional
status('Checking out submodules')
git('submodule', 'sync')
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
