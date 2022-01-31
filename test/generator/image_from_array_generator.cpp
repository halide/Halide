#include "Halide.h"

namespace {

class ImageFromArray : public Halide::Generator<ImageFromArray> {
public:
    Output<Buffer<int32_t, 1>> output{"output"};

    void generate() {
        // Currently the test just exercises halide_image.h.
        Var x;
        output(x) = x;
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(ImageFromArray, image_from_array)
