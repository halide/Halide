#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    if (!get_jit_target_from_environment().has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }

    Func f;
    Var x, y;

    ImageParam im(Int(32), 1);

    const int B = 16;
    const int N = 1024 * 16;
    Expr blocks = im.width() / B;

    f(x, y) = 0;

    f.compute_root().gpu_blocks(y).gpu_threads(x);

    // Sum-scan within each block of size B.
    RDom r1(0, B);
    f(r1, y) = im(y * B + r1) + f(r1 - 1, y);
    f.update(0).gpu_blocks(y);

    // Sum-scan along the last element of each block into a scratch space just before the start of each block.
    RDom r2(1, blocks - 1);
    f(-1, r2) = f(B - 1, r2 - 1) + f(-1, r2 - 1);
    f.update(1).gpu_single_thread();

    // Add the last element of the previous block to everything in each row
    RDom r3(0, B);
    f(r3, y) += f(-1, y);
    f.update(2).gpu_blocks(y).gpu_threads(r3);

    // Read out the output
    Func out;
    out(x) = f(x % B, x / B);
    Var xi;
    out.gpu_tile(x, xi, B);

    // Only deal with inputs that are a multiple of B
    out.bound(x, 0, im.width() / B * B);

    Buffer<int> input = lambda(x, cast<int>(floor((sin(x)) * 100))).realize({N});

    im.set(input);
    Buffer<int> output = out.realize({N});

    int correct = 0;
    for (int i = 0; i < N; i++) {
        correct += input(i);
        if (output(i) != correct) {
            printf("output(%d) = %d instead of %d\n",
                   i, output(i), correct);
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
