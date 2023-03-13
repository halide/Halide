#include "Halide.h"

using namespace Halide;

class CountOps : public Internal::IRMutator {
    Expr visit(const Internal::Reinterpret *op) override {
        std::cerr << Expr(op) << " " << op->type.lanes() << " " << op->value.type().lanes() << "\n";
        if (op->type.lanes() != op->value.type().lanes()) {
            std::cerr << "Got one\n";
            reinterprets++;
        }
        return Internal::IRMutator::visit(op);
    }

    Expr visit(const Internal::Call *op) override {
        if (op->is_intrinsic(Internal::Call::concat_bits)) {
            concats++;
        } else if (op->is_intrinsic(Internal::Call::extract_bits)) {
            extracts++;
        }
        return Internal::IRMutator::visit(op);
    }

public:
    int extracts = 0, concats = 0, reinterprets = 0;
};

int main(int argc, char **argv) {
    for (bool vectorize : {false, true}) {
        // Reinterpret an array of a wide type as a larger array of a smaller type
        Func f, g;
        Var x;

        f(x) = cast<uint32_t>(x);

        // Reinterpret to a narrower type.
        g(x) = extract_bits<uint8_t>(f(x / 4), 8 * (x % 4));

        f.compute_root();

        if (vectorize) {
            f.vectorize(x, 8);
            // The align_bounds directive is critical so that the x%4 term above collapses.
            g.align_bounds(x, 4).vectorize(x, 32);

            // An alternative to the align_bounds call:
            // g.output_buffer().dim(0).set_min(0);
        }

        CountOps counter;
        g.add_custom_lowering_pass(&counter, nullptr);

        Buffer<uint8_t> out = g.realize({1024});
        std::cerr << counter.extracts << " " << counter.reinterprets << " " << counter.concats << "\n";

        if (vectorize) {
            if (counter.extracts > 0) {
                printf("Saw an unwanted extract_bits call in lowered code\n");
                return 1;
            } else if (counter.reinterprets == 0) {
                printf("Did not see a vector reinterpret in lowered code\n");
                return 1;
            }
        }

        for (uint32_t i = 0; i < (uint32_t)out.width(); i++) {
            uint8_t correct = (i / 4) >> (8 * (i % 4));
            if (out(i) != correct) {
                printf("out(%d) = %d instead of %d\n", i, out(i), correct);
                return 1;
            }
        }
    }

    for (bool vectorize : {false, true}) {
        // Reinterpret an array of a narrow type as a smaller array of a wide type
        Func f, g;
        Var x;

        f(x) = cast<uint8_t>(x);

        g(x) = concat_bits({f(4 * x), f(4 * x + 1), f(4 * x + 2), f(4 * x + 3)});

        f.compute_root();

        if (vectorize) {
            f.vectorize(x, 32);
            g.vectorize(x, 8);
        }

        CountOps counter;
        g.add_custom_lowering_pass(&counter, nullptr);

        Buffer<uint32_t> out = g.realize({64});

        if (counter.concats > 0) {
            printf("Saw an unwanted concat_bits call in lowered code\n");
            return 1;
        } else if (counter.reinterprets == 0) {
            printf("Did not see a vector reinterpret in lowered code\n");
            return 1;
        }

        for (int i = 0; i < 64; i++) {
            for (int b = 0; b < 4; b++) {
                uint8_t correct = i * 4 + b;
                uint8_t result = (out(i) >> (b * 8)) & 0xff;
                if (result != correct) {
                    printf("out(%d) byte %d = %d instead of %d\n", i, b, result, correct);
                    return 1;
                }
            }
        }
    }

    // Also test cases that aren't expected to fold into reinterprets
    {
        Func f;
        Var x("x");
        f(x) = cast<uint16_t>(x);

        auto check = [&](const Expr &a, const Expr &b) {
            Func g;
            g(x) = cast<uint8_t>(a == b);
            Buffer<uint8_t> out = g.realize({1024});
            for (int i = 0; i < out.width(); i++) {
                if (out(i) == 0) {
                    std::cerr << "Mismatch between: " << a << " and " << b << " when x == " << i << "\n";
                    exit(1);
                }
            }
        };

        // concat_bits is little-endian
        check(concat_bits({f(x), cast<uint16_t>(37)}), cast<uint32_t>(f(x)) + (37 << 16));
        check(concat_bits({cast<uint16_t>(0), f(x), cast<uint16_t>(0), cast<uint16_t>(0)}), cast(UInt(64), f(x)) << 16);

        // extract_bits is equivalent to right shifting and then casting to a narrower type
        check(extract_bits<uint8_t>(f(x), 3), cast<uint8_t>(f(x) >> 3));

        // Extract bits zero-fills out-of-range bits
        check(extract_bits<uint16_t>(f(x), 3), f(x) >> 3);
        check(extract_bits<int16_t>(f(x), 8), (f(x) >> 8) & 0xff);
        check(extract_bits<uint8_t>(f(x), -1), cast<uint8_t>(f(x)) << 1);

        // MSB of the mantissa of an ieee float
        check(extract_bits<uint8_t>(cast<float>(f(x)), 15), cast<uint8_t>(reinterpret<uint32_t>(cast<float>(f(x))) >> 15));
    }

    printf("Success!\n");
    return 0;
}
