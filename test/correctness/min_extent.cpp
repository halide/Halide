// Test whether min[] and extent[] of an ImageParam are correctly passed into
// the filter.
#include <stdio.h>
#include <Halide.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x("x");
    Func f("f");
    ImageParam in(Int(32), 1, "in");
    Expr factor("factor");

    // Multiply by -10 or +10 for coordinates that fall outside the input
    // image.
    factor = select(x < in.xmin(), -10,
                    select(x >= in.xmin() + in.width(), 10, 1));
    f(x) = factor * x;

    // Create input and output buffers. The input pixels are never accessed,
    // but we initialize them anyway.
    Image<int> input(5);
    Image<int> out(10);
    for (int i=0; i<input.width(); i++)
        input(i) = 0;
    for (int i=0; i<out.width(); i++)
        out(i) = 0;

    // Change coordinate origin of input and output buffer so that they are
    // aligned as follows:
    // input         |------|
    // out     |-----------------|
    const int INOFF = 4;
    const int OUTOFF = 1;
    in.set(input);
    input.set_min(INOFF);
    out.set_min(OUTOFF);
    f.realize(out);

    // Check correctness of result
    int expected[] = { -10, -20, -30, 4, 5, 6, 7, 8, 90, 100 };
    for (int i=0; i<out.width(); i++) {
        if (out(i+OUTOFF) != expected[i]) {
            printf("Unexpected output: %d != %d\n", out(i+OUTOFF), expected[i]);
            return 1;
        }
    }

    return 0;
}
