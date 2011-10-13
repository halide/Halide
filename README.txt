Summary
-------
- Install OCaml (3.12+)
  (On Mac, just use Homebrew: brew install objective-caml.)

- Fetch submodules (LLVM, etc.):

    git submodule update --init

  or if you also want Clang:

    git submodule update --init --recursive

- Build LLVM:

    cd llvm
    ./configure --enable-assertions --enable-targets=all
    make -j12 # parallel build will run much faster

- test the FImage toplevel:

    cd ../src
    ocamlbuild fimage.top  # ...should complete successfully


Build env notes
---------------
- ensure clang sub-submodule (llvm/tools/clang) is updated and checked out at branch matching llvm (optional: if you want to have a clang for this llvm build)
- `source ./bash_profile` to configure llvm paths for current session (optional: if you want llvm tools in your path for manual use)
- put ignores in [llvm,llvm/tools/clang]/.git/info/exclude to keep built dir clean
- configure llvm for debug+asserts during development:
    `./configure --enable-assertions`
- enable all targets (e.g. for arm):
    `./configure --enable-targets=all`


Building and running a simple test
----------------------------------
Given `my_program.ml` which defines some IR, a schedule, then lowers and codegenerates it to `my_program.bc` (currently Cf. `test_lower.ml`):

    # build it
    ocamlbuild my_program.byte
    # run it
    ./my_program.byte [my program args]
    # OR: build and run it together
    ocamlbuild my_program.byte -- [my program args]

    # inspect the LLVM IR
    cat my_program.bc | llvm-dis

    # inspect it with some optimization passes
    opt [args, e.g. -O3] -S my_program.bc

    # compile it to some machine assembly
    llc my_program.bc
    cat my_program.s

    # link it with your own test program
    # (my_test.cpp has an extern declaration for the function defined in my_program.s)
    g++ my_test.cpp my_program.s


Debugging notes
---------------
To make OCaml print useful stack trace info on uncaught exceptions, build a bytecode target with debugging (<target>.d.byte) and set the OCAMLRUNPARAM environment variable to include the "b" (backtrace) option. (Cf. [the OCaml runtime system docs](http://caml.inria.fr/pub/docs/manual-ocaml/manual024.html#toc96) for more.)
