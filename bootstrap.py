#!/usr/bin/env python
# Requires Python 2.6 for the subprocess module

import subprocess
from os import chdir
from os.path import isfile

import os

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


check_call = subprocess.check_call
try:
    check_output = subprocess.check_output
except:
    # backport from https://gist.github.com/1027906
    def _check_output(*popenargs, **kwargs):
        r"""Run command with arguments and return its output as a byte string.

        Backported from Python 2.7 as it's implemented as pure python on stdlib.

        >>> check_output(['/usr/bin/python', '--version'])
        Python 2.6.2
        """
        process = subprocess.Popen(stdout=subprocess.PIPE, *popenargs, **kwargs)
        output, unused_err = process.communicate()
        retcode = process.poll()
        if retcode:
            cmd = kwargs.get("args")
            if cmd is None:
                cmd = popenargs[0]
            error = subprocess.CalledProcessError(retcode, cmd)
            error.output = output
            raise error
        return output
    check_output = _check_output

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

# TODO: make install in subdir, with docs
#        requires graphviz, doxygen; target ocamlbuild to alt dir?; make clean?
# Build llvm
if check_llvm():
    status('llvm appears to be present -- skipping')
else:
    chdir('llvm')
    llvm_cfg = ['--enable-assertions', '--enable-targets=all', '--enable-docs', '--enable-doxygen']
    cfg_str = ' '.join(llvm_cfg)
    status('''Configuring llvm:
    %s''' % cfg_str)
    check_call(['./configure'] + llvm_cfg)
    status('Building llvm')
    check_call('make -j12'.split(' '))
    chdir('docs')
    check_call('make ocamldoc'.split(' '))
    # optional: check_call('make doxygen'.split(' '))
    chdir('../..')
    assert check_llvm()

# Test building 
chdir('src')
status('Test: building fimage.cma')
check_call('ocamlbuild fimage.cma'.split(' '))
