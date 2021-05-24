#include "Halide.h"

namespace {

class Alias : public Halide::Generator<Alias> {
public:
    GeneratorParam<int32_t> offset{"offset", 0};
    Input<Buffer<int32_t>> input{"input", 2};
    Output<Buffer<int32_t>> output{"output", 2};

    void generate() {
        Var x, y;

        Func f, g, h;
        f(x, y) = max(1, (input(x, y) * 17) / 13);
        g(x, y) = x * y * f(x, y);
        h(x, y) = g(x, y) / f(x, y);

        f.compute_at(g, y);
        g.compute_at(h, y);
        if (offset == 0) {
            const int v = 4;
            f.vectorize(x, v);
            g.vectorize(x, v);
            h.vectorize(x, v);
        }

        output = g;
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Alias, alias)
