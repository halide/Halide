#include "Halide.h"

namespace {

class RDomInput : public Halide::Generator<RDomInput> {
public:
    Input<Buffer<uint8_t>> input{"input", 2};

    Func build() {
        RDom r(input);

        // Note: this is terrible way to process all the pixels
        // in an image: do not imitate this code. It exists solely
        // to verify that RDom() accepts an Input<Buffer<>> as well a
        // plain Buffer<>.
        Func output;
        Var x, y;
        output(x, y) = cast<uint8_t>(0);
        output(r.x, r.y) += input(r.x, r.y) ^ cast<uint8_t>(0xff);
        return output;
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(RDomInput, rdom_input)

