#!/usr/bin/env python

from subprocess import check_call,check_output
import os
import json

# TODO: make pch: `g++ -x c++-header -I ../ImageStack/src test_plugin.h`
EPSILON = 0.001

verbose = False
dry_run = False

if dry_run:
    check_call = lambda c: status('run', ' '.join(c))
    check_output = lambda c: status('run', ' '.join(c))

def status(name, s):
    print '%s: %s' % (name, s)

def run(cmd):
    global verbose
    if verbose:
        print '------------------------------------------------------------'
        print (' '.join(cmd))+':'
        print '------------------------------------------------------------'
        #print check_call(cmd)
    res = check_output(cmd)
    if verbose:
        print res
    return res

proj_root = os.path.join('..', '..')
llvm_path = os.path.join(proj_root, 'llvm', 'Debug+Asserts', 'bin')
llc_exe = os.path.join(llvm_path, 'llc')
clang_exe = os.path.join(llvm_path, 'clang++')
imagestack_path = os.path.join(proj_root, 'ImageStack', 'bin')
imagestack_exe = os.path.join(imagestack_path, 'ImageStack')
cxx_exe = 'g++'

def test(name):
    status(name, 'Building bitcode')

    # Set up filenames
    cfgfile = "%s.json" % name

    bcfile = "%s.bc" % name
    asmfile = "%s.s" % name
    sofile = "%s.so" % name

    outfile = "%s.png" % name
    logfile = "%s.log" % name
    timefile = "%s.time" % name

    # Build the ocaml test
    target = '%s/%s.byte' % (name, name)
    cmd = "ocamlbuild -no-links %s" % target
    run(cmd.split(' '))
    
    # Dive in
    os.chdir(name)
    
    # Clean up old cruft
    os.remove(bcfile)
    os.remove(asmfile)
    os.remove(sofile)
    os.remove(outfile)
    os.remove(logfile)
    os.remove(timefile)

    # Codegen the bitcode
    run("../_build/%s" % target)

    # Compile the plugin
    status(name, 'Building plugin')
    # Codegen the bitcode
    run([llc_exe, "-O3", "-disable-cfi", bcfile])
    # Load plugin info from generated json
    opts = json.load(open(cfgfile))
    # TODO: move helpstr, num_popped into externs imported straight from the LLVM module?
    assert opts['name'] == name
    opts['classname'] = 'Test'+name.title()
    opts['namestr'] = 'test_'+name
    # Compile the thing
    cmd = [
        cxx_exe,
        "-O3",
        "-Xlinker", "-dylib", "-Xlinker", "-undefined", "-Xlinker", "dynamic_lookup",
        "-DCLASSNAME=%(classname)s" % opts,
        "-DNUM_POPPED=%(num_popped)d" % opts,
        "-DHELPSTR=\"%(helpstr)s\"" % opts,
        "-DNAMESTR=\"%(namestr)s\"" % opts,
        "../test_plugin.cpp",
        asmfile,
        "-I../../ImageStack/src",
        "-o", sofile
    ]
    run(cmd)

    # Run the plugin
    status(name, 'Running plugin')
    cmd = [
        imagestack_exe,
        '-plugin', sofile,
    ]
    cmd = cmd + opts['before_run'] + ['-test_%s' % name] + opts['args'] + ['-save', outfile]
    cmd = cmd + opts['validation']
    out = run(cmd)

    # TODO: change this to actually redirect stdout, stderr to .log and .err while running
    # Save stdout to <name>.log
    with open(logfile, "w") as f:
        f.write(out)

    # Expect result as float in last line of output
    try:
        residual = float(out.splitlines()[-1])
        assert(residual < EPSILON)
        print "."
    except:
        print "%s failed!" % name
        if verbose:
            print "Output:\n%s" % out

    # Pop out
    os.chdir("..")

test('brightness')
