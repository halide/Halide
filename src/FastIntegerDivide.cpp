#include <mutex>

#include "FastIntegerDivide.h"
#include "IROperator.h"
#include "IntegerDivisionTable.h"

namespace Halide {

using namespace Halide::Internal::IntegerDivision;

namespace IntegerDivideTable {

Buffer<uint8_t> integer_divide_table_u8() {
    static std::mutex initialize_lock;
    std::lock_guard<std::mutex> lock_guard(initialize_lock);
    {
        static Buffer<uint8_t> im(256, 2);
        static bool initialized = false;
        if (!initialized) {
            initialized = true;
            for (size_t i = 0; i < 256; i++) {
                im(i, 0) = table_runtime_u8[i][2];
                im(i, 1) = table_runtime_u8[i][3];
            }
        }
        return im;
    }
}

Buffer<uint8_t> integer_divide_table_s8() {
    static std::mutex initialize_lock;
    std::lock_guard<std::mutex> lock_guard(initialize_lock);
    {
        static Buffer<uint8_t> im(256, 2);
        static bool initialized = false;
        if (!initialized) {
            initialized = true;
            for (size_t i = 0; i < 256; i++) {
                im(i, 0) = table_runtime_s8[i][2];
                im(i, 1) = table_runtime_s8[i][3];
            }
        }
        return im;
    }
}

Buffer<uint16_t> integer_divide_table_u16() {
    static std::mutex initialize_lock;
    std::lock_guard<std::mutex> lock_guard(initialize_lock);
    {
        static Buffer<uint16_t> im(256, 2);
        static bool initialized = false;
        if (!initialized) {
            initialized = true;
            for (size_t i = 0; i < 256; i++) {
                im(i, 0) = table_runtime_u16[i][2];
                im(i, 1) = table_runtime_u16[i][3];
            }
        }
        return im;
    }
}

Buffer<uint16_t> integer_divide_table_s16() {
    static std::mutex initialize_lock;
    std::lock_guard<std::mutex> lock_guard(initialize_lock);
    {
        static Buffer<uint16_t> im(256, 2);
        static bool initialized = false;
        if (!initialized) {
            initialized = true;
            for (size_t i = 0; i < 256; i++) {
                im(i, 0) = table_runtime_s16[i][2];
                im(i, 1) = table_runtime_s16[i][3];
            }
        }
        return im;
    }
}

Buffer<uint32_t> integer_divide_table_u32() {
    static std::mutex initialize_lock;
    std::lock_guard<std::mutex> lock_guard(initialize_lock);
    {
        static Buffer<uint32_t> im(256, 2);
        static bool initialized = false;
        if (!initialized) {
            initialized = true;
            for (size_t i = 0; i < 256; i++) {
                im(i, 0) = table_runtime_u32[i][2];
                im(i, 1) = table_runtime_u32[i][3];
            }
        }
        return im;
    }
}

Buffer<uint32_t> integer_divide_table_s32() {
    static std::mutex initialize_lock;
    std::lock_guard<std::mutex> lock_guard(initialize_lock);
    {
        static Buffer<uint32_t> im(256, 2);
        static bool initialized = false;
        if (!initialized) {
            initialized = true;
            for (size_t i = 0; i < 256; i++) {
                im(i, 0) = table_runtime_s32[i][2];
                im(i, 1) = table_runtime_s32[i][3];
            }
        }
        return im;
    }
}
}  // namespace IntegerDivideTable

Expr fast_integer_divide(Expr numerator, Expr denominator) {
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

    Type wide = t.with_bits(t.bits() * 2);

    Expr result;
    if (t.is_uint()) {
        Expr mul, shift;
        switch(t.bits()) {
        case 8:
        {
            Buffer<uint8_t> table = IntegerDivideTable::integer_divide_table_u8();
            mul = table(denominator, 0);
            shift = table(denominator, 1);
            break;
        }
        case 16:
        {
            Buffer<uint16_t> table = IntegerDivideTable::integer_divide_table_u16();
            mul = table(denominator, 0);
            shift = table(denominator, 1);
            break;
        }
        default: // 32
        {
            Buffer<uint32_t> table = IntegerDivideTable::integer_divide_table_u32();
            mul = table(denominator, 0);
            shift = table(denominator, 1);
            break;
        }
        }

        // Multiply-keep-high-half
        result = (cast(wide, mul) * numerator);

        if (t.bits() < 32) result = result / (1 << t.bits());
        else result = result >> t.bits();

        result = cast(t, result);

        // Add half the difference between input and output so far
        result = result + (numerator - result)/2;

        // Do a final shift
        result = result >> shift;


    } else {

        Expr mul, shift;
        switch(t.bits()) {
        case 8:
        {
            Buffer<uint8_t> table = IntegerDivideTable::integer_divide_table_s8();
            mul = table(denominator, 0);
            shift = table(denominator, 1);
            break;
        }
        case 16:
        {
            Buffer<uint16_t> table = IntegerDivideTable::integer_divide_table_s16();
            mul = table(denominator, 0);
            shift = table(denominator, 1);
            break;
        }
        default: // 32
        {
            Buffer<uint32_t> table = IntegerDivideTable::integer_divide_table_s32();
            mul = table(denominator, 0);
            shift = table(denominator, 1);
            break;
        }
        }

        // Extract sign bit
        //Expr xsign = (t.bits() < 32) ? (numerator / (1 << (t.bits()-1))) : (numerator >> (t.bits()-1));
        Expr xsign = select(numerator > 0, cast(t, 0), cast(t, -1));

        // If it's negative, flip the bits of the
        // numerator. Equivalent to:
        // select(numerator < 0, -(numerator+1), numerator);
        numerator = xsign ^ numerator;

        // Multiply-keep-high-half
        result = (cast(wide, mul) * numerator);
        if (t.bits() < 32) result = result / (1 << t.bits());
        else result = result >> t.bits();
        result = cast(t, result);

        // Do the final shift
        result = result >> shift;

        // Maybe flip the bits again
        result = xsign ^ result;
    }

    // The tables don't work for denominator == 1
    result = select(denominator == 1, numerator, result);

    internal_assert(result.type() == t);

    return result;
}

Expr fast_integer_modulo(Expr numerator, Expr denominator) {
    return numerator - fast_integer_divide(numerator, denominator) * denominator;
}

}  // namespace Halide
