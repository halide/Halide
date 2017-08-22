#include "Halide.h"

namespace {

class ImageFromArray : public Halide::Generator<ImageFromArray> {
public:
   Func build() {
        // Currently the test just exercises halide_image.h.
        Var x;
        Func f;
        f(x) = x;
        return f;
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(ImageFromArray, image_from_array)

