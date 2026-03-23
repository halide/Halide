
#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {

    Func f{"f"}, g{"g"}, h{"h"};
    Var x;

    f(x) = cast<uint8_t>(x + 7);
    g(x) = f(x);
    h(x) = g(x);

    // The schedule below is an error. It should really be:
    // f.store_root().compute_at(g, Var::outermost());
    // So that it's nested inside the consumer h.
    f.store_root().compute_at(h, x);
    g.store_root().compute_at(h, x).async();

    Buffer<uint8_t> buf = h.realize({32});
    for (int i = 0; i < buf.dim(0).extent(); i++) {
        uint8_t correct = i + 7;
        if (buf(i) != correct) {
            printf("buf(%d) = %d instead of %d\n", i, buf(i), correct);
            return 1;
        }
    }

    return 0;
}
