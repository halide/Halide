# The ancestor (parent) directory already loaded HalideCompiler as `shared`
# (see ../CMakeLists.txt, multi_dir_TEST_CONFLICT). Requesting `static` here
# must fail cleanly rather than silently keep the ancestor's `shared` flavor.
find_package(Halide REQUIRED COMPONENTS static JIT)
