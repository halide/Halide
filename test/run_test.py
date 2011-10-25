from subprocess import check_call,check_output
import os
import json

verbose = False

def status(name, s):
    print '%s: %s' % (name, s)

def run(cmd):
    global verbose
    if not verbose:
        check_output(cmd)
    else:
        print '------------------------------------------------------------'
        print (' '.join(cmd))+':'
        print '------------------------------------------------------------'
        print check_call(cmd)

proj_root = os.path.join('..', '..')
llvm_path = os.path.join(proj_root, 'llvm', 'Debug+Asserts', 'bin')
clang_exe = os.path.join(llvm_path, 'clang++')
imagestack_path = os.path.join(proj_root, 'ImageStack', 'bin')
imagestack_exe = os.path.join(imagestack_path, 'ImageStack')

def build_kernel(name):
    status(name, 'Building bitcode')

    # Build the ocaml test
    target = '%s/%s.byte' % (name, name)
    cmd = "ocamlbuild -no-links %s" % target
    run(cmd.split(' '))
    
    # Dive in
    os.chdir(name)
    
    # Codegen the bitcode
    run("../_build/%s" % target)

    # Compile the plugin
    status(name, 'Building plugin')
    # TODO: load name, num_popped, helpstr from json?
    opts = json.load(open('%s.json' % name))
    assert opts['name'] == name
    opts['classname'] = 'Test'+name.title()
    opts['namestr'] = 'test_'+name
    #opts = {
        #'classname': 'Test'+name.title(),
        #'num_popped': 1,
        #'namestr': 'test_'+name,
        #'helpstr': 'help: %s' % name
    #}
    cmd = [
        clang_exe,
        "-O3",
        "-Xlinker", "-dylib", "-Xlinker", "-undefined", "-Xlinker", "dynamic_lookup",
        "-DCLASSNAME=%(classname)s" % opts,
        "-DNUM_POPPED=%(num_popped)d" % opts,
        "-DHELPSTR=\"%(helpstr)s\"" % opts,
        "-DNAMESTR=\"%(namestr)s\"" % opts,
        "../test_plugin.cpp",
        "%s.bc" % name,
        "-I../../ImageStack/src",
        "-o", "%s.so" % name
    ]
    run(cmd)

    # Run the plugin
    status(name, 'Running plugin')
    check_call([
        imagestack_exe,
        '-plugin', '%s.so' % name,
        '-help', 'test_%s' % name
    ])
    
    # Pop out
    os.chdir("..")

build_kernel('brightness')
