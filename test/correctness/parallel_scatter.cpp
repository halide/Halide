#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    for (int parallel = 0; parallel < 2; parallel++) {
        // Splatting a value is not an associative or commutative binary
        // op, but if it's non-recursive then it's safe to
        // parallelize/reorder. If we parallelize it we need "atomic".

        Func squares;
        Var b, x;
        squares(x) = x * x;

        Func hist;
        hist(b, x) = 0;

        // Make some sort of histogram where we leave all non-square
        // locations as zero, and set all perfect squares to any value
        // that does not depend on the reduction domain.
        RDom r(0, 100);
        hist(squares(r) % 10, x) = squares(x);

        // Race conditions should be safe for this definition. The
        // scatters collide, but all races are races to write the same
        // value to the same site.

        RVar ro, ri;
        hist.update().split(r, ro, ri, 8).reorder(ro, x, ri);
        if (parallel) {
            hist.update().atomic().parallel(ri).parallel(x).vectorize(ro);
        }

        Buffer<int> result = hist.realize(10, 100);
        for (int i = 0; i < result.width(); i++) {
            for (int j = 0; j < result.height(); j++) {
                // If i has a square root in the integers modulo ten
                // (i.e. is there a perfect square that ends with the
                // given digit?), then we expect to see a value.
                bool has_square_root[] = {true /* 0 */, true /* 1 */, false, false, true /* 4 */,
                                          true /* 25 */, true /* 36 */, false, false, true /* 9 */};
                int correct = has_square_root[i] ? (j * j) : 0;
                if (result(i, j) != correct) {
                    printf("result(%d, %d) = %d instead of %d\n",
                           i, j, result(i, j), correct);
                    return 1;
                }
            }
        }
    }

    printf("Success!\n");

    return 0;
}
