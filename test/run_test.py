#!/usr/bin/env python

from subprocess import check_call,check_output,CalledProcessError
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

def remove(filename):
    try: os.remove(filename)
    except: pass

proj_root = os.path.join('..', '..')
llvm_path = os.path.join(proj_root, 'llvm', 'Release+Asserts', 'bin')
llc_exe = os.path.join(llvm_path, 'llc')
clang_exe = os.path.join(llvm_path, 'clang++')
imagestack_path = os.path.join(proj_root, 'ImageStack', 'bin')
imagestack_exe = os.path.join(imagestack_path, 'ImageStack')
cxx_exe = 'g++-4.6'

def test_cpp(name):

    status(name, "Building FImage.a")
    # Make sure FImage.a is built
    cmd = "make -C ../cpp_bindings/ FImage.a"
    run(cmd.split(' '))

    curdir = os.getcwd()

    # Dive in
    os.chdir(name)

    srcfile = "test.cpp"
    logfile = "log.txt"
    
    # Clean up old stuff
    remove("generated.o")
    remove("generated.bc")
    remove("passes.txt")
    remove("a.out")
    remove(logfile)

    status(name, "Compiling %s" % srcfile)
    compile_cmd_base = [cxx_exe,
         "-std=c++0x",
         "-I../../../cpp_bindings/",
         "../../../cpp_bindings/FImage.a",
         "-Wl,-dead_strip"]
    try:
        compile_cmd = compile_cmd_base + \
            ["-L/usr/local/cuda/lib", "-lcuda", srcfile]
        status(name, " ".join(compile_cmd))
        run(compile_cmd)
    except CalledProcessError:
        compile_cmd = compile_cmd_base + [srcfile]
        status(name, "retry without CUDA: "+ " ".join(compile_cmd))
        run(compile_cmd)

    # Run the test
    try:
        status(name, "Running test")
        out = run(["./a.out"])
        print "."
    except CalledProcessError:
        print "%s failed!" % name
        if verbose:
            print "Output:\n%s" % out
    finally:
        # Save the log
        with open(logfile, "w") as f:
            f.write(out)

    # Pop back up
    os.chdir(curdir)

def test(name):
    if name.startswith('cpp/'): return test_cpp(name)

    status(name, 'Building bitcode')

    # Set up filenames
    cfgfile = "%s.json" % name

    bcfile = "%s.bc" % name
    asmfile = "%s.s" % name
    sofile = "%s.so" % name

    outfile = "%s.png" % name
    tmpfile = "%s.tmp" % name
    logfile = "%s.log" % name
    timefile = "%s.time" % name

    # Build the ocaml test
    target = '%s/%s.byte' % (name, name)
    cmd = "ocamlbuild -no-links %s" % target
    run(cmd.split(' '))
    
    
    # Dive in
    os.chdir(name)
    
    # Clean up old cruft
    remove(bcfile)
    remove(asmfile)
    remove(sofile)
    remove(outfile)
    remove(logfile)
    remove(timefile)

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
    cmd = cmd + opts['before_run'] + ['-test_%s' % name] + opts['args'] + ['-save', outfile] + ['-save', tmpfile]
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


import sys
if len(sys.argv) > 1:
    for x in sys.argv[1:]: test(x)
else:
    test('brightness')
    test('cpp/vector_cast')
    test('cpp/vectorize')
