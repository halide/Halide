#include "Halide.h"

namespace {

class Shuffler : public Halide::Generator<Shuffler> {
public:
    Input<Buffer<int32_t, 1>> input{"input"};
    Output<Buffer<int32_t, 1>> output{"output"};

    void generate() {
        // The +1 is just to get a Broadcast node
        output(x) = upsample(upsample(input))(x) + 1;
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
