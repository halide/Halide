Build env notes:

- ensure clang sub-submodule (llvm/tools/clang) is updated and checked out at branch matching llvm
- build llvm submodule with `./configure --prefix=$(PWD)`
- `source ./bash_profile` to configure llvm paths for current session
- put ignores in [llvm,llvm/tools/clang]/.git/info/exclude to keep built dir clean
