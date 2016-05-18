#include "Halide.h"

namespace {

class ImageRuntimeType : public Halide::Generator<ImageRuntimeType> {
public:
   Func build() {
        // Currently the test just exercises halide_image.h.
        Var x;
        Func f;
        f(x) = x;
        return f;
    }
};

Halide::RegisterGenerator<ImageRuntimeType> register_my_gen{"image_runtime_type"};

}  // namespace
