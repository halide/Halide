Build env notes:
----------------

- ensure clang sub-submodule (llvm/tools/clang) is updated and checked out at branch matching llvm
- build llvm submodule with `./configure --prefix=$(PWD)/build-Debug+Asserts`
- `source ./bash_profile` to configure llvm paths for current session
- put ignores in [llvm,llvm/tools/clang]/.git/info/exclude to keep built dir clean
- configure llvm for debug+asserts during development:
    `./configure --enable-assertions`
- enable all targets (e.g. for arm):
    `./configure --enable-targets=all`

CamlImages is used for loading/saving test images in OCaml. It is expected to be build and installed on the system from the ./camlimages/ subdirectory using omake. (It is not yet referenced directly from the subproject since it is not a major dependency, and simply installing it seems sufficient for now.)

Debugging notes:
----------------
To make OCaml print useful stack trace info on uncaught exceptions, build a bytecode target with debugging (<target>.d.byte) and set the OCAMLRUNPARAM environment variable to include the "b" (backtrace) option. (Cf. [the OCaml runtime system docs](http://caml.inria.fr/pub/docs/manual-ocaml/manual024.html#toc96) for more.)
