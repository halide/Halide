#include "Halide.h"

using namespace Halide;

std::mt19937 rng(0);

int main(int argc, char **argv) {

    int fuzz_seed = argc > 1 ? atoi(argv[1]) : time(nullptr);
    rng.seed(fuzz_seed);
    printf("Lerp test seed: %d\n", fuzz_seed);

    // Lerp lowering incorporates a cast. This test checks that a widening lerp
    // is equal to the widened version of the lerp.
    for (Type t1 : {UInt(8), UInt(16), UInt(32), Int(8), Int(16), Int(32), Float(32)}) {
        if (rng() & 1) continue;
        for (Type t2 : {UInt(8), UInt(16), UInt(32), Float(32)}) {
            if (rng() & 1) continue;
            for (Type t3 : {UInt(8), UInt(16), UInt(32), Int(8), Int(16), Int(32), Float(32)}) {
                if (rng() & 1) continue;
                Func f;
                Var x;
                f(x) = cast(t1, random_uint((int)rng()));

                Expr weight = cast(t2, f(x + 16));
                if (t2.is_float()) {
                    weight /= 256.f;
                    weight = clamp(weight, 0.f, 1.f);
                }

                Expr zero_val = f(x);
                Expr one_val = f(x + 8);
                Expr lerped = lerp(zero_val, one_val, weight);

                Func cast_and_lerp, lerp_alone, cast_of_lerp;
                cast_and_lerp(x) = cast(t3, lerped);
                lerp_alone(x) = lerped;
                cast_of_lerp(x) = cast(t3, lerp_alone(x));

                RDom r(0, 32 * 1024);
                Func check;
                check() = maximum(abs(cast<double>(cast_and_lerp(r)) -
                                      cast<double>(cast_of_lerp(r))));

                f.compute_root().vectorize(x, 8, TailStrategy::RoundUp);
                lerp_alone.compute_root().vectorize(x, 8, TailStrategy::RoundUp);
                cast_and_lerp.compute_root().vectorize(x, 8, TailStrategy::RoundUp);
                cast_of_lerp.compute_root().vectorize(x, 8, TailStrategy::RoundUp);

                double err = evaluate<double>(check());

                if (err > 1e-5) {
                    printf("Difference of lerp + cast and lerp alone is %f,"
                           " which exceeds threshold for seed %d\n",
                           err, fuzz_seed);
                    return -1;
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}
