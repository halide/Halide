#include <stdio.h>
#include <Halide.h>
#include <math.h>

using namespace Halide;
using std::vector;

// Make sure the interleave pattern generates good vector code

int main(int argc, char **argv) {
    Func f, g, h;
    Var x;
    
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

    printf("Success!\n");
    return 0;
}
