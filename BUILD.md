# WARNING: THIS DESCRIPTION IS INCOMPLETE #

More thorough directions for Mac and Linux are coming very shortly. We are also working to automate as much as possible, to keep this simple.

Changes in process
--------------------
I am adding dependencies on OCaml batteries included and sexplib. On Ubuntu or similar, these are available via:

	sudo apt-get install libsexplib-camlp4-dev ocaml-batteries-included

On OS X, I recommend using [OPAM](http://opam.ocamlpro.com/) to install additional OCaml packages (sexplib, batteries). With Homebrew: `brew install opam`.

Summary
-------
- Install OCaml (3.12.* or 4.00.*)  
  On Mac, use Homebrew: `brew install objective-caml`, 
  or MacPorts: `sudo port install ocaml`)

- Install ocamlfind (aka `ocaml-findlib` in Ubuntu and other package sources). If it's not in your package manager's database, it's easy to just download/configure/install by hand: http://projects.camlcity.org/projects/findlib.html.

- Install sexplib and batteries, as mentioned above. (The build will soon be updated to fetch and build these in a project-local directory and reference them internally, so you will no longer have to install them yourself. For now, though, it's still necessary.)

- Make sure you have some `clang` >= 3.0 in your path. (This is required by one build step under `Halide/src`.) We no longer build our own under Halide/llvm, so it's easiest to just have Xcode 4.x or `apt-get install clang`.

- `./bootstrap` to build everything (and wait -- this builds all of LLVM, which takes a while)

- `cd test; ./run_test cpp/*`

- Check out `test/cpp/*` and `apps/*` for examples of how to get started.  
  **NOTE**: some of the apps have out of date build rules expecting g++-4.6/4.7 (not standard on OS X), or semantics which tickle new error detection passes in the Halide compiler. Don't worry if some of them don't work out of the box. This should return to normal soon.

- _OPTIONAL:_ `source ./bash_profile` to configure llvm paths for current session (if you want llvm tools in your path for manual use, or for use by static build `Makefile`s like those in some of `apps/*`).  
  **TODO: hard code LLVM paths in Makefiles to make this really optional again.**


Build env notes
---------------
- ensure clang sub-submodule (llvm/tools/clang) is updated and checked out at branch matching llvm (optional: if you want to have a clang for this llvm build)
- put ignores in [llvm,llvm/tools/clang]/.git/info/exclude to keep built dir clean
- configure llvm for debug+asserts during development:
    `./configure --enable-assertions`
- enable all targets (e.g. for arm):
    `./configure --enable-targets=all`


Debugging notes
---------------
To make OCaml print useful stack trace info on uncaught exceptions, build a bytecode target with debugging (<target>.d.byte) and set the OCAMLRUNPARAM environment variable to include the "b" (backtrace) option. (Cf. [the OCaml runtime system docs](http://caml.inria.fr/pub/docs/manual-ocaml/manual024.html#toc96) for more.)
