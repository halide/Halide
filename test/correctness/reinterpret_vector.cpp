#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

class CheckNoVectorMath : public IRMutator {
public:
    using IRMutator::mutate;
    Expr mutate(const Expr &e) override {
        IRMutator::mutate(e);
        // An allow-list of IR nodes we are OK with
        if (e.type().is_vector() &&
            !(Call::as_intrinsic(e, {Call::reinterpret}) ||
              e.as<Load>() ||
              e.as<Ramp>() ||
              e.as<Variable>() ||
              e.as<Broadcast>())) {
            std::cout << "Unexpected vector expression: " << e << "\n";
            exit(-1);
        }

        return e;
    }
};

int main(int argc, char **argv) {
    // Check we can treat a vector of a wide type as a wider vector of a
    // narrower type for free.
    Var x, y, c;

    // Treat a 32-bit image as a twice-as-wide 16-bit image
    {
        Func narrow, wide;
        wide(x, y) = cast<uint32_t>(x + y);
        narrow(x, y) = select(x % 2 == 0,
                              cast<uint16_t>(wide(x / 2, y)),
                              cast<uint16_t>(wide(x / 2, y) >> 16));
        wide.compute_root();
        narrow.align_bounds(x, 16).vectorize(x, 16);
        CheckNoVectorMath checker;
        narrow.add_custom_lowering_pass(&checker, nullptr);

        Buffer<uint16_t> out = narrow.realize({1024, 1024});

        for (int y = 0; y < out.height(); y++) {
            for (int x = 0; x < out.width(); x++) {
                int correct = ((x % 2 == 0) ? x / 2 + y : (x / 2 + y) >> 16);
                if (out(x, y) != correct) {
                    printf("out(%d, %d) = %d instead of %d\n", x, y, out(x, y), correct);
                    return -1;
                }
            }
        }
    }

    // Treat 2-dimensional 32-bit values representing rgba as 3-dimensional rgba
    {
        Func rgba_packed, rgba;
        rgba_packed(x, y) = cast<uint32_t>(x + y);
        rgba(c, x, y) = mux(c, {cast<uint8_t>(rgba_packed(x, y)),
                                cast<uint8_t>(rgba_packed(x, y) >> 8),
                                cast<uint8_t>(rgba_packed(x, y) >> 16),
                                cast<uint8_t>(rgba_packed(x, y) >> 24)});
        rgba_packed.compute_root();
        rgba.align_bounds(x, 16).vectorize(x, 16).bound(c, 0, 4).unroll(c);
        rgba.output_buffer().dim(1).set_stride(4);
        CheckNoVectorMath checker;
        rgba.add_custom_lowering_pass(&checker, nullptr);

        Buffer<uint8_t> out = rgba.realize({3, 1024, 1024});

        for (int y = 0; y < out.dim(2).extent(); y++) {
            for (int x = 0; x < out.dim(1).extent(); x++) {
                for (int c = 0; c < out.dim(0).extent(); c++) {
                    uint8_t correct = (c == 0) ? (x + y) :
                                      (c == 1) ? (x + y) >> 8 :
                                      (c == 2) ? (x + y) >> 16 :
                                                 (x + y) >> 24;
                    if (out(c, x, y) != correct) {
                        printf("out(%d, %d, %d) = %d instead of %d\n", c, x, y, out(x, y), correct);
                        return -1;
                    }
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}
