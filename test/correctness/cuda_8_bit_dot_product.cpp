#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f;
    Var x, y;

    f(x, y) = cast<int8_t>(x + y);
    f.compute_root();

    Func g;
    RDom r(0, 16);
    g(x, y) += cast<int32_t>(f(r, x)) * f(r, y);

    Func h;
    h(x, y) = g(x, y);

    Var xi, yi;
    g.update().atomic().vectorize(r, 4).unroll(r);
    h.gpu_tile(x, y, xi, yi, 32, 8);

    h.compile_jit(Target{"host-cuda-cuda_capability_61"});

    Buffer<int32_t> out(1024, 1024);
    h.realize(out);
    out.device_sync();

    return 0;
}
