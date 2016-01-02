#include "Halide.h"

namespace {

class Pyramid : public Halide::Generator<Pyramid> {
public:
    ImageParam input{ Float(32), 2, "input" };
    GeneratorParam<int> levels {"levels", 10};

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
        std::vector<Func> pyramid(levels);
        Func in = Halide::BoundaryConditions::repeat_edge(input);

        pyramid[0](x, y) = in(x, y);

        for (int i = 1; i < 10; i++) {
            pyramid[i](x, y) = downsample(pyramid[i-1])(x, y);
        }

        for (int i = 0; i < 10; i++) {
            pyramid[i].compute_root().parallel(y);
            // Vectorize if we're still wide enough at this level
            pyramid[i].specialize(pyramid[i].output_buffer().width() >= 8).vectorize(x, 8);
        }

        return pyramid;
    }
};

Halide::RegisterGenerator<Pyramid> register_my_gen{"pyramid"};

}  // namespace
