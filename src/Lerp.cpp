#include <algorithm>
#include <cmath>

#include "CSE.h"
#include "IR.h"
#include "IROperator.h"
#include "Lerp.h"
#include "Simplify.h"
#include "Target.h"

namespace Halide {
namespace Internal {

Expr lower_lerp(Expr zero_val, Expr one_val, const Expr &weight, const Target &target) {

    Expr result;

    internal_assert(zero_val.type() == one_val.type());
    internal_assert(weight.type().is_uint() || weight.type().is_float())
        << "Bad weight type: " << weight.type() << "\n";

    Type result_type = zero_val.type();

    Expr bias_value = make_zero(result_type);
    Type computation_type = result_type;

    if (zero_val.type().is_int()) {
        computation_type = UInt(zero_val.type().bits(), zero_val.type().lanes());
        // We must take care to do the addition and subtraction of the
        // bias while in the unsigned computation type, where overflow
        // is well-defined.
        bias_value = cast(computation_type, result_type.min());
    }

    // For signed integer types, just convert everything to unsigned
    // and then back at the end to ensure proper rounding, etc.
    // There is likely a better way to handle this.
    if (result_type != computation_type) {
        zero_val = Cast::make(computation_type, zero_val) - bias_value;
        one_val = Cast::make(computation_type, one_val) - bias_value;
    }

    if (result_type.is_bool()) {
        Expr half_weight;
        if (weight.type().is_float()) {
            half_weight = 0.5f;
        } else {
            half_weight = weight.type().max() / 2;
        }

        result = select(weight > half_weight, one_val, zero_val);
    } else {
        Expr typed_weight;
        Expr inverse_typed_weight;

        if (weight.type().is_float()) {
            typed_weight = weight;
            if (computation_type.is_uint()) {
                // TODO: Verify this reduces to efficient code or
                // figure out a better way to express a multiply
                // of unsigned 2^32-1 by a double promoted weight
                if (computation_type.bits() == 32) {
                    typed_weight =
                        Cast::make(computation_type,
                                   cast<double>(Expr(65535.0f)) * cast<double>(Expr(65537.0f)) *
                                       Cast::make(Float(64, typed_weight.type().lanes()), typed_weight));
                } else {
                    typed_weight =
                        Cast::make(computation_type,
                                   computation_type.max() * typed_weight);
                }
                inverse_typed_weight = computation_type.max() - typed_weight;
            } else {
                inverse_typed_weight = make_one(computation_type) - typed_weight;
            }

        } else {
            if (computation_type.is_float()) {
                int weight_bits = weight.type().bits();
                Expr denom = make_const(computation_type, (ldexp(1.0, weight_bits) - 1));
                typed_weight = Cast::make(computation_type, weight) / denom;
                inverse_typed_weight = make_one(computation_type) - typed_weight;
            } else {
                // This code rescales integer weights to the right number of bits.
                // It takes advantage of (2^n - 1) == (2^(n/2) - 1)(2^(n/2) + 1)
                // e.g. 65535 = 255 * 257. (Ditto for the 32-bit equivalent.)
                // To recale a weight of m bits to be n bits, we need to do:
                //     scaled_weight = (weight / (2^m - 1)) * (2^n - 1)
                // which power of two values for m and n, results in a series like
                // so:
                //     (2^(m/2) + 1) * (2^(m/4) + 1) ... (2^(n*2) + 1)
                // The loop below computes a scaling constant and either multiples
                // or divides by the constant and relies on lowering and llvm to
                // generate efficient code for the operation.
                int bit_size_difference = weight.type().bits() - computation_type.bits();
                if (bit_size_difference == 0) {
                    typed_weight = weight;
                } else {
                    typed_weight = Cast::make(computation_type, weight);

                    int bits_left = ::abs(bit_size_difference);
                    int shift_amount = std::min(computation_type.bits(), weight.type().bits());
                    uint64_t scaling_factor = 1;
                    while (bits_left != 0) {
                        internal_assert(bits_left > 0);
                        scaling_factor = scaling_factor + (scaling_factor << shift_amount);
                        bits_left -= shift_amount;
                        shift_amount *= 2;
                    }
                    if (bit_size_difference < 0) {
                        typed_weight =
                            Cast::make(computation_type, weight) *
                            cast(computation_type, (int32_t)scaling_factor);
                    } else {
                        typed_weight =
                            Cast::make(computation_type,
                                       weight / cast(weight.type(), (int32_t)scaling_factor));
                    }
                }
                inverse_typed_weight =
                    Cast::make(computation_type,
                               computation_type.max() - typed_weight);
            }
        }

        if (computation_type.is_float()) {
            result = (zero_val * inverse_typed_weight +
                      one_val * typed_weight);
        } else {
            int32_t bits = computation_type.bits();
            switch (bits) {
            case 1:
                result = select(typed_weight, one_val, zero_val);
                break;
            case 8:
            case 16:
            case 32: {
                Expr prod_sum = (widening_mul(zero_val, inverse_typed_weight) +
                                 widening_mul(one_val, typed_weight));
                // Now we need to do a rounding divide and narrow. For
                // 8-bit, this rounding divide looks like (x + 127) /
                // 255. On most platforms it's we can compute this as
                // ((x + 128) / 256 + x + 128) / 256. Note that
                // overflow is impossible here because the most our
                // prod_sum can be is 255^2.
                if (target.arch == Target::X86) {
                    // On x86 we have no rounding shifts but we do
                    // have a multiply-keep-high-half. So it's
                    // actually one instruction cheaper to do the
                    // division directly.
                    Expr divisor = cast(UInt(bits), -1);
                    result = (prod_sum + divisor / 2) / divisor;
                } else {
                    result = rounding_shift_right(rounding_shift_right(prod_sum, bits) + prod_sum, bits);
                }
                result = Cast::make(UInt(bits, computation_type.lanes()), result);
                break;
            }
            case 64:
                // TODO: 64-bit lerp is not supported as current approach
                // requires double-width multiply.
                // There is an informative error message in IROperator.h.
                internal_error << "Can't do a 64-bit lerp.\n";
                break;
            default:
                break;
            }
        }

        if (!is_const_zero(bias_value)) {
            result = Cast::make(result_type, result + bias_value);
        }
    }

    return simplify(common_subexpression_elimination(result));
}

}  // namespace Internal
}  // namespace Halide
