#include "Halide.h"

namespace {

class EmbedImage : public Halide::Generator<EmbedImage> {
public:
    ImageParam input{ Float(32), 3, "input" };

    Func build() {
        Buffer<float> matrix(3, 3);

        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                matrix(i, j) = 0.0f;
            }
        }
        // Make the matrix a flip-channels-and-multiply-by-0.5 so that this is easy to test
        matrix(2, 0) = matrix(1, 1) = matrix(0, 2) = 0.5f;

        Func f;
        Var x, y, c;
        RDom j(0, 3);
        f(x, y, c) = sum(matrix(j, c) * input(x, y, j));
        return f;
    }
};

Halide::RegisterGenerator<EmbedImage> register_my_gen{"embed_image"};

}  // namespace
