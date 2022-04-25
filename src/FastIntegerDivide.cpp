#include <mutex>

#include "FastIntegerDivide.h"
#include "IROperator.h"
#include "IntegerDivisionTable.h"

namespace Halide {

using namespace Halide::Internal;
using namespace Halide::Internal::IntegerDivision;

namespace {

int shift_for_denominator(uint32_t d) {
    return 63 - clz64(d - 1);
}

Expr shift_for_denominator(const Expr &d) {
    internal_assert(d.type().element_of() == UInt(8));
    return 7 - count_leading_zeros(d - 1);
}

Buffer<uint8_t> integer_divide_table_u8() {
    static auto im = []() {
        Buffer<uint8_t> im(256);
        for (uint32_t i = 0; i < 256; i++) {
            im(i) = table_runtime_u8[i][2];
            if (i > 1) {
                internal_assert(table_runtime_u8[i][3] == shift_for_denominator(i));
            }
        }
        return im;
    }();
    return im;
}

Buffer<uint8_t> integer_divide_table_s8() {
    static auto im = []() {
        Buffer<uint8_t> im(256);
        for (uint32_t i = 0; i < 256; i++) {
            im(i) = table_runtime_s8[i][2];
            if (i > 1) {
                internal_assert(table_runtime_s8[i][3] == shift_for_denominator(i));
            }
        }
        return im;
    }();
    return im;
}

Buffer<uint8_t> integer_divide_table_srz8() {
    static auto im = []() {
        Buffer<uint8_t> im(256);
        for (uint32_t i = 0; i < 256; i++) {
            im(i) = table_runtime_srz8[i][2];
            if (i > 1) {
                internal_assert(table_runtime_srz8[i][3] == shift_for_denominator(i));
            }
        }
        return im;
    }();
    return im;
}

Buffer<uint16_t> integer_divide_table_u16() {
    static auto im = []() {
        Buffer<uint16_t> im(256);
        for (uint32_t i = 0; i < 256; i++) {
            im(i) = table_runtime_u16[i][2];
            if (i > 1) {
                internal_assert(table_runtime_u16[i][3] == shift_for_denominator(i));
            }
        }
        return im;
    }();
    return im;
}

Buffer<uint16_t> integer_divide_table_s16() {
    static auto im = []() {
        Buffer<uint16_t> im(256);
        for (uint32_t i = 0; i < 256; i++) {
            im(i) = table_runtime_s16[i][2];
            if (i > 1) {
                internal_assert(table_runtime_s16[i][3] == shift_for_denominator(i));
            }
        }
        return im;
    }();
    return im;
}

Buffer<uint16_t> integer_divide_table_srz16() {
    static auto im = []() {
        Buffer<uint16_t> im(256);
        for (uint32_t i = 0; i < 256; i++) {
            im(i) = table_runtime_srz16[i][2];
            if (i > 1) {
                internal_assert(table_runtime_srz16[i][3] == shift_for_denominator(i));
            }
        }
        return im;
    }();
    return im;
}

Buffer<uint32_t> integer_divide_table_u32() {
    static auto im = []() {
        Buffer<uint32_t> im(256);
        for (uint32_t i = 0; i < 256; i++) {
            im(i) = table_runtime_u32[i][2];
            if (i > 1) {
                internal_assert(table_runtime_u32[i][3] == shift_for_denominator(i));
            }
        }
        return im;
    }();
    return im;
}

Buffer<uint32_t> integer_divide_table_s32() {
    static auto im = []() {
        Buffer<uint32_t> im(256);
        for (uint32_t i = 0; i < 256; i++) {
            im(i) = table_runtime_s32[i][2];
            if (i > 1) {
                internal_assert(table_runtime_s32[i][3] == shift_for_denominator(i));
            }
        }
        return im;
    }();
    return im;
}

Buffer<uint32_t> integer_divide_table_srz32() {
    static auto im = []() {
        Buffer<uint32_t> im(256);
        for (uint32_t i = 0; i < 256; i++) {
            im(i) = table_runtime_srz32[i][2];
            if (i > 1) {
                internal_assert(table_runtime_srz32[i][3] == shift_for_denominator(i));
            }
        }
        return im;
    }();
    return im;
}

Expr fast_integer_divide_impl(Expr numerator, Expr denominator, bool round_to_zero) {
    if (is_const(denominator)) {
        // There's code elsewhere for this case.
        return numerator / cast<uint8_t>(denominator);
    }
    user_assert(denominator.type() == UInt(8))
        << "Fast integer divide requires a UInt(8) denominator\n";
    Type t = numerator.type();
    user_assert(t.is_uint() || t.is_int())
        << "Fast integer divide requires an integer numerator\n";
    user_assert(t.bits() == 8 || t.bits() == 16 || t.bits() == 32)
        << "Fast integer divide requires a numerator with 8, 16, or 32 bits\n";

    Type wide = t.widen();

    Expr result;
    if (t.is_uint()) {
        Expr mul, shift = shift_for_denominator(denominator);
        switch (t.bits()) {
        case 8: {
            Buffer<uint8_t> table = integer_divide_table_u8();
            mul = table(denominator);
            break;
        }
        case 16: {
            Buffer<uint16_t> table = integer_divide_table_u16();
            mul = table(denominator);
            break;
        }
        default:  // 32
        {
            Buffer<uint32_t> table = integer_divide_table_u32();
            mul = table(denominator);
            break;
        }
        }

        // Multiply-keep-high-half
        result = (cast(wide, mul) * numerator);

        if (t.bits() < 32) {
            result = result / (1 << t.bits());
        } else {
            result = result >> Internal::make_const(result.type(), t.bits());
        }

        result = cast(t, result);

        // Add half the difference between input and output so far
        result = result + (numerator - result) / 2;

        // Do a final shift
        result = result >> cast(result.type(), shift);

    } else if (!round_to_zero) {

        Expr mul, shift = shift_for_denominator(denominator);
        switch (t.bits()) {
        case 8: {
            Buffer<uint8_t> table = integer_divide_table_s8();
            mul = table(denominator);
            break;
        }
        case 16: {
            Buffer<uint16_t> table = integer_divide_table_s16();
            mul = table(denominator);
            break;
        }
        default:  // 32
        {
            Buffer<uint32_t> table = integer_divide_table_s32();
            mul = table(denominator);
            break;
        }
        }

        // Extract sign bit
        // Expr xsign = (t.bits() < 32) ? (numerator / (1 << (t.bits()-1))) : (numerator >> (t.bits()-1));
        Expr xsign = select(numerator > 0, cast(t, 0), cast(t, -1));

        // If it's negative, flip the bits of the
        // numerator. Equivalent to:
        // select(numerator < 0, -(numerator+1), numerator);
        numerator = xsign ^ numerator;

        // Multiply-keep-high-half
        result = (cast(wide, mul) * numerator);
        if (t.bits() < 32) {
            result = result / (1 << t.bits());
        } else {
            result = result >> Internal::make_const(result.type(), t.bits());
        }
        result = cast(t, result);

        // Do the final shift
        result = result >> cast(result.type(), shift);

        // Maybe flip the bits again
        result = xsign ^ result;
    } else {
        // Signed round to zero
        Expr mul, shift = shift_for_denominator(denominator);
        switch (t.bits()) {
        case 8: {
            Buffer<uint8_t> table = integer_divide_table_srz8();
            mul = table(denominator);
            break;
        }
        case 16: {
            Buffer<uint16_t> table = integer_divide_table_srz16();
            mul = table(denominator);
            break;
        }
        default:  // 32
        {
            Buffer<uint32_t> table = integer_divide_table_srz32();
            mul = table(denominator);
            break;
        }
        }

        // Extract sign bit
        // Expr xsign = (t.bits() < 32) ? (numerator / (1 << (t.bits()-1))) : (numerator >> (t.bits()-1));
        Expr xsign = select(numerator > 0, cast(t, 0), cast(t, -1));

        // Multiply-keep-high-half
        result = (cast(wide, mul) * numerator);
        if (t.bits() < 32) {
            result = result / (1 << t.bits());
        } else {
            result = result >> Internal::make_const(result.type(), t.bits());
        }
        result = cast(t, result);

        // Do the final shift
        result = result >> cast(result.type(), shift);

        // Add one if the numerator was negative
        result -= xsign;
    }

    // The tables don't work for denominator == 1
    result = select(std::move(denominator) == 1, std::move(numerator), result);

    internal_assert(result.type() == t);

    return result;
}

}  // namespace

Expr fast_integer_divide_round_to_zero(const Expr &numerator, const Expr &denominator) {
    return fast_integer_divide_impl(numerator, denominator, /** round to zero **/ true);
}

Expr fast_integer_divide(const Expr &numerator, const Expr &denominator) {
    return fast_integer_divide_impl(numerator, denominator, /** round to zero **/ false);
}

Expr fast_integer_modulo(const Expr &numerator, const Expr &denominator) {
    Expr ratio = fast_integer_divide(numerator, denominator);
    return numerator - ratio * denominator;
}

}  // namespace Halide
