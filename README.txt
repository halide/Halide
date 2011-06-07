Build env notes:

- ensure clang sub-submodule (llvm/tools/clang) is updated and checked out at branch matching llvm
- build llvm submodule with `./configure --prefix=$(PWD)/build-Debug+Asserts`
- `source ./bash_profile` to configure llvm paths for current session
- put ignores in [llvm,llvm/tools/clang]/.git/info/exclude to keep built dir clean
- configure llvm for debug+asserts during development:
    `./configure --enable-assertions`
- enable all targets (e.g. for arm):
    `./configure --enable-targets=all`
