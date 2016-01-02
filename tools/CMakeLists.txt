# This executable does not have any generators registered so it is only capable
# of generating a standalone object file containing Halide's runtime.
#
# This is used for pure runtime tests which need to link against the Halide
# runtime.
halide_project(runtime.generator "generator" GenGen.cpp)
