# This file should NOT be installed.
# It is used by python_bindings (and future externalizable projects) to satisfy
# calls to `find_package(Halide)` when used in-tree.

message(VERBOSE "Spoofing find_package(Halide) since in-tree builds already have Halide available.")
set(Halide_FOUND 1)
