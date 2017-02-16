#include "Halide.h"

using namespace Halide;

using std::vector;
using std::pair;

int main(int argc, char **argv) {
    Func input;
    Var x, y;
    input(x, y) = random_float();
    input.compute_root();

    const int levels = 20;

    // Make a laplacian pyramid that truncates at a certain size to
    // probe for compile-time issues with skip stages, allocation
    // bounds inference, etc, arising from deeply nested selects in
    // params. Jam in as many types of if statement as we can.

    Func pyr_down[levels];
    Param<int> width, height;
    vector<pair<Expr, Expr> > sizes(levels);
    sizes[0] = { width, height };
    for (int i = 1; i < levels; i++) {
        sizes[i] = { (sizes[i-1].first + 1)/2, (sizes[i-1].second + 1)/2 };
    }
    pyr_down[0] = input;
    for (int i = 1; i < levels; i++) {
        Func bounded = BoundaryConditions::repeat_edge(pyr_down[i-1], {{0, sizes[i].first}, {0, sizes[i].second}});
        // Some simple stencil that acts like a 4x4 kernel for the purpose of bounds inference.
        Func downsampled;
        downsampled(x, y) = bounded(2*x-1, 2*y-1) + bounded(2*x+2, 2*y+2);

        // Only compute it if the pyramid level is large enough.
        pyr_down[i](x, y) = select(max(sizes[i].first, sizes[i].second) > 5,
                                   downsampled(x, y),
                                   0.0f);

        // Specialize it, to introduce another type of condition in the params.
        pyr_down[i].compute_root()
            .specialize(max(width, height) > 32)
            .vectorize(x, 16)
            .parallel(y, 16, TailStrategy::GuardWithIf);
    }

    Func pyr_up[levels];
    pyr_up[levels-1] = pyr_down[levels-1];
    for (int i = levels-2; i >= 0; i--) {
        Func upsample;
        upsample(x, y) = pyr_up[i+1](x/2 - 1, y/2 - 1) + pyr_up[i+1](x/2 + 1, y/2 + 1);

        // Mask it with a select
        pyr_up[i](x, y) = select(max(sizes[i].first, sizes[i].second) > 5,
                                 pyr_down[i](x, y) - upsample(x, y),
                                 pyr_down[i](x, y));


        pyr_up[i].compute_root()
            .specialize(max(width, height) > 32)
            .vectorize(x, 16)
            .parallel(y, 16, TailStrategy::GuardWithIf);

    }

    // It's sufficient to just realize this. Compilation will take the
    // age of the universe if anything combinatorial is going on.
    width.set(1000);
    height.set(1000);
    pyr_up[0].realize(1000, 1000);

    return 0;
}
