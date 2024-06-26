#include "Halide.h"

namespace {

class RDomInput : public Halide::Generator<RDomInput> {
public:
    Input<Buffer<uint8_t, 2>> input{"input"};
    Output<Buffer<uint8_t, 2>> output{"output"};

    void generate() {
        RDom r(input);

        // Note: this is terrible way to process all the pixels
        // in an image: do not imitate this code. It exists solely
        // to verify that RDom() accepts an Input<Buffer<>> as well a
        // plain Buffer<>.
        Var x, y;
        output(x, y) = cast<uint8_t>(0);
        output(r.x, r.y) += input(r.x, r.y) ^ cast<uint8_t>(0xff);

        RDom r2(output);  // unused, just here to ensure it compiles
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(RDomInput, rdom_input)
