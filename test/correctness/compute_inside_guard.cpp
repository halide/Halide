
#include "Halide.h"
#include "halide_benchmark.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {

    ImageParam input{UInt(8), 1, "input"};

    Var x;

    Func intermediate;
    intermediate(x) = input(x);

    Func output;
    output(x) = intermediate(x);

    Var xo, xi;
    output
        .compute_root()
        .split(x, xo, xi, output.output_buffer().width() / 8, TailStrategy::GuardWithIf);

    // If we compute intermediate at xi, there should be precisely one
    // value of the intermediate needed, so we can put it in
    // register-class storage. This is a way to test if the compiler
    // realizes that there is precisely one value of the intermediate
    // needed, which will only work out if the realization of
    // intermediate lands inside the if statement created by the
    // output's GuardWithIf split.
    intermediate
        .compute_at(output, xi)
        .unroll(x)
        .store_in(MemoryType::Register);

    // Also check that the bounds required of the input haven't been
    // rounded up to a multiple of the split factor (which is not a
    // constant, just to make things even harder).
    Buffer<uint8_t> ibuf(123);
    Buffer<uint8_t> obuf(123);

    input.set(ibuf);
    output.realize(obuf);

    printf("Success!\n");
    return 0;
}
