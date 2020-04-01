#include <memory>
#include <type_traits>
#include <vector>

#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f;
    Var x;
    f(x) = Tuple(x, sin(x));

    // Don't do this. Instead constrain the size of output buffer 0.
    f.output_buffers()[1].dim(0).set_min(4);

    f.compile_jit();

    return 0;
}
