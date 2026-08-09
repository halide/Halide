// This translation unit exists only so that other test executables can
// reuse its precompiled <Halide.h> header via target_precompile_headers's
// REUSE_FROM option (see test/CMakeLists.txt and cmake/HalideTestHelpers.cmake).
#include "Halide.h"

int main() {
    return 0;
}
