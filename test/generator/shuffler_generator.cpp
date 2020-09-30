#include "Halide.h"

namespace {

class Shuffler : public Halide::Generator<Shuffler> {
public:
    Input<Buffer<int32_t>> input{"input", 1};
    Output<Buffer<int32_t>> output{"output", 1};

    void generate() {
        output = upsample(upsample(input));
        output.vectorize(x, natural_vector_size<int32_t>());
    }

private:
    Func upsample(Func f) {
        Func u;
        u(x) = f(x / 2 + 1);
        return u;
    }

    Var x;
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Shuffler, shuffler)
