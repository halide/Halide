
#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;
using namespace Halide::ConciseCasts;

int main(int argc, char **argv) {

    ImageParam im(UInt(8), 1);
    Func f;
    Var x;

    f(x) = im(x);

    im.dim(0).set_stride(Expr());
    f.specialize(im.dim(0).stride() == 1);
    f.specialize(im.dim(0).stride() == 2);
    f.specialize_fail("unreachable");

    Callable c = f.compile_to_callable({im});

    Halide::Runtime::Buffer<uint8_t> in_buf(nullptr, {halide_dimension_t{0, 0, 0}});
    Halide::Runtime::Buffer<uint8_t> out_buf(32);

    int result = c(in_buf, out_buf);

    if (result != 0) {
        printf("Callable failed: %d\n", result);
        return 1;
    }

    if (in_buf.dim(0).stride() != 1 ||
        in_buf.dim(0).extent() != 32) {
        printf("Unexpected bounds query result. stride = %d, extent = %d\n",
               in_buf.dim(0).stride(),
               in_buf.dim(0).extent());
        return 1;
    }

    printf("Success!\n");

    return 0;
}
