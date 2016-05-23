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

Halide::RegisterGenerator<ImageFromArray> register_my_gen{"image_from_array"};

}  // namespace
