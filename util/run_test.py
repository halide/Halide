#!/usr/bin/env python

from __future__ import with_statement
import os
import os.path
import json
from pbs import Command, make, which, ocamlbuild
import sys

# TODO: make pch: `g++ -x c++-header -I ../ImageStack/src test_plugin.h`
EPSILON = 0.001

verbose = False

def status(name, s):
    print '%s: %s' % (name, s)

def remove(filename):
    try: os.remove(filename)
    except: pass

proj_root = os.path.join('..', '..')
llvm_path = os.path.join(proj_root, 'llvm', 'Release+Asserts', 'bin')
llc = Command(os.path.join(llvm_path, 'llc'))
clang = Command(os.path.join(llvm_path, 'clang++'))
imagestack = Command(os.path.join(proj_root, 'ImageStack', 'bin', 'ImageStack'))
cxx = Command(which('g++-4.6'))

failures = []

def test_cpp(name):
    
    global failures
    
    # status(name, "Building Halide.a")
    # Make sure Halide.a is built
    make('-C', '../cpp_bindings/', 'Halide.a')

    curdir = os.getcwd()

    # Dive in
    os.chdir(name)

    srcfile = "test.cpp"
    logfile = "log.txt"
    errfile = "err.txt"
    
    # Clean up old stuff
    remove("generated.o")
    remove("generated.bc")
    remove("passes.txt")
    remove("a.out")
    remove(logfile)
    remove(errfile)
    
    # Threading these through as file handles shared by all PBS tasks makes
    # sure each task appends to the main logs, and flushes on exception.
    with open(logfile, "wt") as log:
        with open(errfile, "wt") as err:
            # status(name, "Compiling %s" % srcfile)
            compile_cmd = ["-std=c++0x", 
                           "-I../../../cpp_bindings",
                           "-Wno-format",
                           "-fPIC",
                           "-rdynamic",
                           srcfile,                   
                           "../../../cpp_bindings/Halide.a", 
                           "-ldl", "-lpthread"]

            if os.path.exists("/usr/local/cuda"):
                compile_cmd = compile_cmd + ["-L/usr/local/cuda/lib", "-L/usr/lib/nvidia-current", "-lcuda"]
            
            try:
                # Build the test
                # status(name, " ".join(compile_cmd))
                cxx(compile_cmd, _out=log, _err=err)
                # Run the test
                # status(name, "Running test")
                tester = Command("./a.out")
                tester(_out=log, _err=err)
                sys.stdout.write(".")
                sys.stdout.flush()
            except:
                sys.stdout.write("E")
                sys.stdout.flush()
                failures.append(name)

    # Pop back up
    os.chdir(curdir)

def test(name):
    if name.startswith('cpp/'): return test_cpp(name)

    # status(name, 'Building bitcode')

    # Set up filenames
    cfgfile = "%s.json" % name

    bcfile = "%s.bc" % name
    asmfile = "%s.s" % name
    sofile = "%s.so" % name

    outfile = "%s.png" % name
    tmpfile = "%s.tmp" % name
    logfile = "%s.log" % name
    errfile = "%s.err" % name
    timefile = "%s.time" % name

    # Build the ocaml test
    target = '%s/%s.byte' % (name, name)
    cmd = "-no-links %s" % target
    ocamlbuild(cmd.split(' '))
    
    # Dive in
    os.chdir(name)
    
    # Clean up old cruft
    remove(bcfile)
    remove(asmfile)
    remove(sofile)
    remove(outfile)
    remove(logfile)
    remove(errfile)
    remove(timefile)
    
    # Codegen the bitcode
    runner = Command("../_build/%s" % target)
    runner()

    # Compile the plugin
    # status(name, 'Building plugin')
    # Codegen the bitcode
    llc("-O3", "-disable-cfi", bcfile)
    # Load plugin info from generated json
    opts = json.load(open(cfgfile))
    # TODO: move helpstr, num_popped into externs imported straight from the LLVM module?
    assert opts['name'] == name
    opts['classname'] = 'Test'+name.title()
    opts['namestr'] = 'test_'+name
    # Compile the thing
    cmd = [
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
    cxx(cmd)

    # Run the plugin
    # status(name, 'Running plugin')
    cmd = [
        '-plugin', sofile,
    ]
    cmd = cmd + opts['before_run'] + ['-test_%s' % name] + opts['args'] + ['-save', outfile] + ['-save', tmpfile]
    cmd = cmd + opts['validation']
    out = imagestack(cmd, _out=logfile, _err=errfile)

    # Expect result as float in last line of output
    try:
        residual = float(out.splitlines()[-1])
        assert(residual < EPSILON)
        sys.stdout.write(".")
        sys.stdout.flush()
    except:
        print "%s failed!" % name
        if verbose:
            print "Output:\n%s" % out

    # Expect runtime as float (seconds) in some line like "_im_time: %f"
    time = 0.0
    try:
        timestr = "_im_time: "
        times = [l.split(timestr)[-1] for l in out.splitlines() if l.startswith(timestr)]
        time = [float(t) for t in times][0]
        with open(timefile, "w") as f:
            f.write("%f" % time)
    except:
       print "Failed to get time!"

    # Pop out
    os.chdir("..")


if len(sys.argv) > 1:
    for x in sys.argv[1:]: test(x)
else:
    test('brightness')
    test('cpp/vector_cast')
    test('cpp/vectorize')

if not failures:
    print '\n' + '-'*70
    print '\nOK'
    sys.exit(0)
else:
    for name in failures:
        print "\n\n%s failed:" % name
        print "\n%s" % open(os.path.join(name,"log.txt")).read()
        print "\n%s" % open(os.path.join(name,"err.txt")).read()
    sys.exit(-1)
