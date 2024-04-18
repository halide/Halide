Halide-written execution engine for Gemini/Gemma models.

To build with CMake:

- build and install Halide locally to ${HALIDE_INSTALL}
- cd apps/hamal
- mkdir build && cd build
- cmake .. -DHalide_DIR=${HALIDE_INSTALL}/lib/cmake/Halide && ninja

TODO
