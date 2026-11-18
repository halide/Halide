#include "Halide.h"
#include <stdio.h>

using namespace Halide;

// A struct's fields are stored with their own types, so two consecutive
// stores with equal indices can hit different bytes: here m is float16
// element 1 (bytes 2-3) and qh is uint32 element 1 (bytes 4-7). Neither
// store clobbers the other.
int main(int argc, char **argv) {
    Type t = Type::Struct({{"d", Float(16)}, {"m", Float(16)}, {"qh", UInt(32)}});
    Var i("i");
    Func p("p"), out("out");
    p(i) = pack_struct(t, {cast<float16_t>(i + 1.0f), cast<float16_t>(i + 2.0f), cast<uint32_t>(i + 30)});
    out(i) = cast<float>(field(p(i), "d")) + 10 * cast<float>(field(p(i), "m")) + 100 * cast<float>(field(p(i), "qh"));
    p.compute_at(out, i);

    Buffer<float> r = out.realize({8});
    for (int x = 0; x < r.width(); x++) {
        float expected = (x + 1) + 10 * (x + 2) + 100 * (x + 30);
        if (r(x) != expected) {
            printf("out(%d) = %f instead of %f\n", x, r(x), expected);
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
