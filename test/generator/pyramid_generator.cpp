#include "Halide.h"

namespace {

class Pyramid : public Halide::Generator<Pyramid> {
public:
    ImageParam input{ Float(32), 2, "input" };

    Var x, y;

    Func downsample(Func big) {
        Func small;
        small(x, y) = (big(2*x, 2*y) +
                       big(2*x+1, 2*y) +
                       big(2*x, 2*y+1) +
                       big(2*x+1, 2*y+1))/4;
        return small;
    }

    Pipeline build() {
        std::vector<Func> levels(10);
        Func in = Halide::BoundaryConditions::repeat_edge(input);

        levels[0](x, y) = in(x, y);

        for (int i = 1; i < 10; i++) {
            levels[i](x, y) = downsample(levels[i-1])(x, y);
        }

        for (int i = 0; i < 10; i++) {
            levels[i].compute_root().parallel(y);
            // Vectorize if we're still wide enough at this level
            levels[i].specialize(levels[i].output_buffer().width() >= 8).vectorize(x, 8);
        }

        return levels;
    }
};

Halide::RegisterGenerator<Pyramid> register_my_gen{"pyramid"};

}  // namespace
