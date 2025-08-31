#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
class ExtractConcatBitsTest : public ::testing::Test {
protected:
    Var x{"x"};
    Func u16{lambda(x, cast<uint16_t>(x))};
    void check(const Expr &a, const Expr &b) {
        Func g;
        g(x) = cast<uint8_t>(a == b);
        Buffer<uint8_t> out = g.realize({1024});
        for (int i = 0; i < out.width(); i++) {
            EXPECT_EQ(out(i), 1) << "Mismatch between: " << a << " and " << b << " when x == " << i;
        }
    }
};

class CountOps final : public Internal::IRMutator {
    Expr visit(const Internal::Reinterpret *op) override {
        if (op->type.lanes() != op->value.type().lanes()) {
            reinterprets++;
        }
        return IRMutator::visit(op);
    }

    Expr visit(const Internal::Call *op) override {
        if (op->is_intrinsic(Internal::Call::concat_bits)) {
            concats++;
        } else if (op->is_intrinsic(Internal::Call::extract_bits)) {
            extracts++;
        }
        return IRMutator::visit(op);
    }

public:
    int extracts = 0, concats = 0, reinterprets = 0;
};
}  // namespace

TEST_F(ExtractConcatBitsTest, ReinterpretWideToNarrow) {
    for (bool vectorize : {false, true}) {
        SCOPED_TRACE(vectorize ? "vectorized" : "non-vectorized");

        Func f, g;

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

        if (vectorize) {
            EXPECT_EQ(counter.extracts, 0) << "Saw an unwanted concat_bits call in lowered code";
            EXPECT_GT(counter.reinterprets, 0) << "Did not see a vector reinterpret in lowered code";
        }

        for (int i = 0; i < out.width(); i++) {
            EXPECT_EQ(out(i), (i / 4) >> (8 * (i % 4))) << "i = " << i;
        }
    }
}

TEST_F(ExtractConcatBitsTest, ReinterpretNarrowToWide) {
    for (bool vectorize : {false, true}) {
        SCOPED_TRACE(vectorize ? "vectorized" : "non-vectorized");

        Func f, g;

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

        EXPECT_EQ(counter.extracts, 0) << "Saw an unwanted concat_bits call in lowered code";
        EXPECT_GT(counter.reinterprets, 0) << "Did not see a vector reinterpret in lowered code";

        for (int i = 0; i < 64; i++) {
            for (int b = 0; b < 4; b++) {
                uint8_t result = (out(i) >> (b * 8)) & 0xff;
                EXPECT_EQ(result, i * 4 + b) << "i = " << i << " b = " << b;
            }
        }
    }
}

TEST_F(ExtractConcatBitsTest, LittleEndian) {
    check(concat_bits({u16(x), cast<uint16_t>(37)}), cast<uint32_t>(u16(x)) + (37 << 16));
    check(concat_bits({cast<uint16_t>(0), u16(x), cast<uint16_t>(0), cast<uint16_t>(0)}), cast(UInt(64), u16(x)) << 16);
}

TEST_F(ExtractConcatBitsTest, EqualToRightShiftAndCast) {
    check(extract_bits<uint8_t>(u16(x), 3), cast<uint8_t>(u16(x) >> 3));
}

TEST_F(ExtractConcatBitsTest, ZeroFillOutOfRangeBits) {
    check(extract_bits<uint16_t>(u16(x), 3), u16(x) >> 3);
    check(extract_bits<int16_t>(u16(x), 8), (u16(x) >> 8) & 0xff);
    check(extract_bits<uint8_t>(u16(x), -1), cast<uint8_t>(u16(x)) << 1);
}

TEST_F(ExtractConcatBitsTest, ExtractFloatMantissaMSB) {
    check(extract_bits<uint8_t>(cast<float>(u16(x)), 15), cast<uint8_t>(reinterpret<uint32_t>(cast<float>(u16(x))) >> 15));
}
