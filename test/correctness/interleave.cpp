#include <stdio.h>
#include <Halide.h>
#include <math.h>

using namespace Halide;
using std::vector;

// Make sure the interleave pattern generates good vector code

int main(int argc, char **argv) {
    Func f, g, h;
    Var x, y;

    f(x) = sin(x);
    g(x) = cos(x);
    h(x) = select(x % 2 == 0, 1.0f/f(x/2), g(x/2)*17.0f);

    f.compute_root();
    g.compute_root();
    h.vectorize(x, 8);

    h.compile_to_assembly("interleave.s", vector<Argument>());

    Image<float> result = h.realize(16);
    for (int x = 0; x < 16; x++) {
        float correct = ((x % 2) == 0) ? (1.0f/(sinf(x/2))) : (cosf(x/2)*17.0f);
        float delta = result(x) - correct;
        if (delta > 0.01 || delta < -0.01) {
            printf("result(%d) = %f instead of %f\n", x, result(x), correct);
            return -1;
        }
    }

    Func f1, f2, f3, f4, f5;
    Func output;
    f1(x) = sin(x);
    f2(x) = sin(2*x);
    f3(x) = sin(3*x);
    f4(x) = sin(4*x);
    f5(x) = sin(5*x);
    output(x, y) = select(y == 0, f1(x),
                          y == 1, f2(x),
                          y == 2, f3(x),
                          y == 3, f4(x),
                          f5(x));

    output
            .reorder(y, x)
            .bound(y, 0, 5)
            .unroll(y)
            .vectorize(x, 8);

    output.output_buffer()
            .set_min(1, 0)
            .set_stride(0, 5)
            .set_stride(1, 1)
            .set_extent(1, 5);

    output.compile_to_bitcode("interleave_5.bc", vector<Argument>());
    output.compile_to_assembly("interleave_5.s", vector<Argument>());

    Buffer buff;
    buff = Buffer(Float(32), 16, 5, 0, 0, (uint8_t *)0);
    buff.raw_buffer()->stride[0] = 5;
    buff.raw_buffer()->stride[1] = 1;

    Realization r(Internal::vec(buff));
    output.realize(r, get_jit_target_from_environment());

    Image<float> result2 = r[0];
    for (int x = 0; x < 16; x++) {
        for (int y = 0; y < 5; y++) {
            float correct = sin((y+1)*x);
            float delta = result2(x,y) - correct;
            if (delta > 0.01 || delta < -0.01) {
                printf("result(%d) = %f instead of %f\n", x, result2(x,y), correct);
                return -1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
