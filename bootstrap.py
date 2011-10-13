#!/usr/bin/env python
# Requires Python 2.7 for the subprocess module

from subprocess import check_output,check_call
from os import chdir
from os.path import isfile

status_str = '''
%(bars)s
%(status)s
%(bars)s'''

def status(s):
    linelength = max([len(l) for l in s.splitlines()])
    bars = ''.join(['-' for i in range(linelength)])
    print status_str % {'bars': bars, 'status': s}

def check_llvm():
    try:
        llvm_path = './llvm/Debug+Asserts'
        assert isfile(llvm_path+'/lib/ocaml/llvm.cma') # is llvm.cma there?
        check_output([llvm_path+'/bin/llvm-config', '--version']) # try calling llvm-config
        return True
    except:
        return False

# Test for ocaml 3.12.*
status('Testing for OCaml 3.12.*')
ver = check_output(['ocaml', '-version'])
print ver
assert '3.12' in ver
print '...OK!'

# Submodule update/init
# TODO: make --recursive optional
status('Checking out submodules')
check_call('git submodule update --init --recursive'.split(' '))

# Build llvm
if check_llvm():
    status('llvm appears to be present -- skipping')
else:
    chdir('llvm')
    status('''Configuring llvm:
    --enable-assertions --enable-targets=all''')
    check_call('./configure --enable-assertions --enable-targets=all'.split(' '))
    status('Building llvm')
    check_call('make -j12'.split(' '))
    chdir('..')
    assert check_llvm()

# Test building 
chdir('src')
status('Test: building fimage.top')
check_call('ocamlbuild fimage.top'.split(' '))
