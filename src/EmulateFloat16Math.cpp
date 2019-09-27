#include "EmulateFloat16Math.h"
#include "CSE.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Lerp.h"
#include "Simplify.h"

namespace Halide {
namespace Internal {

Expr bfloat16_to_float32(Expr e) {
    if (e.type().is_bfloat()) {
        e = reinterpret(e.type().with_code(Type::UInt), e);
    }
    e = cast(UInt(32, e.type().lanes()), e);
    e = e << 16;
    e = reinterpret(Float(32, e.type().lanes()), e);
    e = strict_float(e);
    return e;
}

Expr float32_to_bfloat16(Expr e) {
    internal_assert(e.type().bits() == 32);
    e = strict_float(e);
    e = reinterpret(UInt(32, e.type().lanes()), e);
    // We want to round ties to even, so before truncating either
    // add 0x8000 (0.5) to odd numbers or 0x7fff (0.499999) to
    // even numbers.
    e += 0x7fff + ((e >> 16) & 1);
    e = (e >> 16);
    e = cast(UInt(16, e.type().lanes()), e);
    e = reinterpret(BFloat(16, e.type().lanes()), e);
    return e;
}


Expr float16_to_float32(Expr value) {
    value = strict_float(value);
    Type f32_t = Float(32, value.type().lanes());
    Type u32_t = UInt(32, value.type().lanes());
    Type u16_t = UInt(16, value.type().lanes());

    Expr f16_bits = value;
    if (!(value.type() == u16_t)) {
        f16_bits = reinterpret(u16_t, f16_bits);
    }

    Expr magnitude = f16_bits & make_const(u16_t, 0x7fff);
    Expr sign = f16_bits & make_const(u16_t, 0x8000);

    // Denorms are linearly spaced, so we should just use an
    // int->float cast and then scale down by reducing the
    // exponent.
    Expr denorm = reinterpret(u32_t, strict_float(cast(f32_t, magnitude))) - 0x0c000000;

    Expr exponent_mantissa = cast(u32_t, magnitude) << 13;
    exponent_mantissa = select(magnitude == 0, 0,
                               magnitude < 0x0400, denorm, // denorms
                               magnitude >= 0x7c00, exponent_mantissa | 0x7f800000, // Map infinity to infinity
                               exponent_mantissa + 0x38000000); // Fix the exponent bias otherwise

    Expr f32 = strict_float(reinterpret(f32_t, (cast(u32_t, sign) << 16) | exponent_mantissa));
    f32 = common_subexpression_elimination(f32);
    return f32;
}

Expr float32_to_float16(Expr value) {
    // We're about the sniff the bits of a float, so we should
    // guard it with strict float to ensure we don't do things
    // like assume it can't be denormal.
    value = strict_float(value);

    Type f32_t = Float(32, value.type().lanes());
    Type f16_t = Float(16, value.type().lanes());
    Type u32_t = UInt(32, value.type().lanes());
    Type u16_t = UInt(16, value.type().lanes());

    Expr bits = reinterpret(u32_t, value);

    // Extract the sign bit
    Expr sign = bits & make_const(u32_t, 0x80000000);
    bits = bits ^ sign;

    // Test the endpoints
    Expr is_denorm = (bits < make_const(u32_t, 0x38800000));
    Expr is_inf = (bits == make_const(u32_t, 0x7f800000));
    Expr is_nan = (bits > make_const(u32_t, 0x7f800000));

    // Denorms are linearly spaced, so we can handle them
    // by scaling up the input as a float and using the
    // existing int-conversion rounding instructions.
    Expr denorm_bits = cast(u16_t, strict_float(round(strict_float(reinterpret(f32_t, bits + 0x0c000000)))));
    Expr inf_bits = make_const(u16_t, 0x7c00);
    Expr nan_bits = make_const(u16_t, 0x7fff);

    // We want to round to nearest even, so we add either
    // 0.5 if the integer part is odd, or 0.4999999 if the
    // integer part is even, then truncate.
    bits += (bits >> 13) & 1;
    bits += 0xfff;
    bits = bits >> 13;
    // Rebias the exponent
    bits -= 0x1c000;
    // Truncate the top bits of the exponent
    bits = bits & 0x7fff;
    bits = select(is_denorm, denorm_bits,
                  is_inf, inf_bits,
                  is_nan, nan_bits,
                  cast(u16_t, bits));
    // Recover the sign bit
    bits = bits | cast(u16_t, sign >> 16);
    return common_subexpression_elimination(reinterpret(f16_t, bits));
}

namespace {

const std::map<std::string, std::string> transcendental_remapping =
    {{"sin_f16", "sin_f32"},
     {"asin_f16", "asin_f32"},
     {"cos_f16", "cos_f32"},
     {"acos_f16", "acos_f32"},
     {"tan_f16", "tan_f32"},
     {"atan_f16", "atan_f32"},
     {"atan2_f16", "atan2_f32"},
     {"sinh_f16", "sinh_f32"},
     {"asinh_f16", "asinh_f32"},
     {"cosh_f16", "cosh_f32"},
     {"acosh_f16", "acosh_f32"},
     {"tanh_f16", "tanh_f32"},
     {"atanh_f16", "atanh_f32"},
     {"sqrt_f16", "sqrt_f32"},
     {"exp_f16", "exp_f32"},
     {"log_f16", "log_f32"},
     {"pow_f16", "pow_f32"},
     {"floor_f16", "floor_f32"},
     {"ceil_f16", "ceil_f32"},
     {"round_f16", "round_f32"},
     {"trunc_f16", "trunc_f32"},
     {"is_nan_f16", "is_nan_f32"},
     {"is_inf_f16", "is_inf_f32"},
     {"is_finite_f16", "is_finite_f32"}};
}  // anonymous namespace


bool is_float16_transcendental(const Call *op) {
    return transcendental_remapping.find(op->name) != transcendental_remapping.end();
}

Expr lower_float16_transcendental_to_float32_equivalent(const Call *op) {
    auto it = transcendental_remapping.find(op->name);
    if (it != transcendental_remapping.end()) {
        std::vector<Expr> new_args(op->args.size());
        for (size_t i = 0; i < op->args.size(); i++) {
            new_args[i] = float16_to_float32(op->args[i]);
        }
        Expr e = Call::make(Float(32, op->type.lanes()), it->second, new_args, op->call_type,
                            op->func, op->value_index, op->image, op->param);
        return float32_to_float16(e);
    } else {
        internal_error << "Unknown float16 transcendental: " << Expr(op) << "\n";
        return Expr();
    }
}

Expr lower_float16_cast(const Cast *op) {
    Type src = op->value.type();
    Type dst = op->type;
    Type f32 = Float(32, dst.lanes());
    Expr val = op->value;

    if (src.is_bfloat()) {
        internal_assert(src.bits() == 16);
        val = bfloat16_to_float32(val);
    } else if (src.is_float() && src.bits() < 32) {
        internal_assert(src.bits() == 16);
        val = float16_to_float32(val);
    }

    if (dst.is_bfloat()) {
        internal_assert(dst.bits() == 16);
        val = float32_to_bfloat16(cast(f32, val));
    } else if (dst.is_float() && dst.bits() < 32) {
        internal_assert(dst.bits() == 16);
        val = float32_to_float16(cast(f32, val));
    }

    return cast(dst, val);
}

}  // namespace Internal
}  // namespace Halide
