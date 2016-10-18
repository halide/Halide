#include "Halide.h"

namespace {

class Pyramid : public Halide::Generator<Pyramid> {
public:
    GeneratorParam<int> levels{"levels", 1};  // deliberately wrong value, must be overridden to 10

    Input<Func> input{ "input", Float(32), 2 };

    Output<Func[]> pyramid{ "pyramid", Float(32), 2 }; 

    void generate() {
        pyramid.resize(levels);

        pyramid[0](x, y) = input(x, y);

        for (size_t i = 1; i < pyramid.size(); i++) {
            pyramid[i](x, y) = downsample(pyramid[i-1])(x, y);
        }
    }

    void schedule() {
        for (Func p : pyramid) {
            // No need to specify compute_root() for outputs
            p.parallel(y);
            // Vectorize if we're still wide enough at this level
            const int v = natural_vector_size<float>();
            p.specialize(p.output_buffer().width() >= v).vectorize(x, v);
        }
     }

private:
    Var x, y;

    Func downsample(Func big) {
        Func small;
        small(x, y) = (big(2*x, 2*y) +
                       big(2*x+1, 2*y) +
                       big(2*x, 2*y+1) +
                       big(2*x+1, 2*y+1))/4;
        return small;
    }
};

Halide::RegisterGenerator<Pyramid> register_my_gen{"pyramid"};

}  // namespace
