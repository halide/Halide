#include "EmulateFloat16Math.h"
#include "CSE.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Lerp.h"
#include "Simplify.h"

namespace Halide {
namespace Internal {

Expr bfloat16_to_float32(Expr e) {
    const int lanes = e.type().lanes();
    if (e.type().is_bfloat()) {
        e = reinterpret(e.type().with_code(Type::UInt), e);
    }
    e = cast(UInt(32, lanes), e);
    e = e << 16;
    e = reinterpret(Float(32, lanes), e);
    e = strict_float(e);
    return e;
}

Expr float_to_bfloat16(Expr e) {
    const int lanes = e.type().lanes();
    e = strict_float(e);

    Expr err;
    // First round to float and record any gain of loss of magnitude
    if (e.type().bits() == 64) {
        Expr f = cast(Float(32, lanes), e);
        err = abs(e) - abs(f);
        e = f;
    } else {
        internal_assert(e.type().bits() == 32);
    }
    e = reinterpret(UInt(32, lanes), e);

    // We want to round ties to even, so if we have no error recorded above,
    // before truncating either add 0x8000 (0.5) to odd numbers or 0x7fff
    // (0.499999) to even numbers. If we have error, break ties using that
    // instead.
    Expr tie_breaker = (e >> 16) & 1;  // 1 when rounding down would go to odd
    if (err.defined()) {
        tie_breaker = ((err == 0) & tie_breaker) | (err > 0);
    }
    e += tie_breaker + 0x7fff;
    e = (e >> 16);
    e = cast(UInt(16, lanes), e);
    e = reinterpret(BFloat(16, lanes), e);
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
                               magnitude < 0x0400, denorm,                           // denorms
                               magnitude >= 0x7c00, exponent_mantissa | 0x7f800000,  // Map infinity to infinity
                               exponent_mantissa + 0x38000000);                      // Fix the exponent bias otherwise

    Expr f32 = strict_float(reinterpret(f32_t, (cast(u32_t, sign) << 16) | exponent_mantissa));
    f32 = common_subexpression_elimination(f32);
    return f32;
}

Expr float_to_float16(Expr value) {
    // We're about the sniff the bits of a float, so we should
    // guard it with strict float to ensure we don't do things
    // like assume it can't be denormal.
    value = strict_float(value);

    const int src_bits = value.type().bits();

    Type float_t = Float(src_bits, value.type().lanes());
    Type f16_t = Float(16, value.type().lanes());
    Type bits_t = UInt(src_bits, value.type().lanes());
    Type u16_t = UInt(16, value.type().lanes());

    Expr bits = reinterpret(bits_t, value);

    // Extract the sign bit
    Expr sign = bits & make_const(bits_t, (uint64_t)1 << (src_bits - 1));
    bits = bits ^ sign;

    // Test the endpoints

    // Smallest input representable as normal float16 (2^-14)
    Expr two_to_the_minus_14 = src_bits == 32 ?
                                   make_const(bits_t, 0x38800000) :
                                   make_const(bits_t, (uint64_t)0x3f10000000000000ULL);
    Expr is_denorm = bits < two_to_the_minus_14;

    // Smallest input too big to represent as a float16 (2^16)
    Expr two_to_the_16 = src_bits == 32 ?
                             make_const(bits_t, 0x47800000) :
                             make_const(bits_t, (uint64_t)0x40f0000000000000ULL);
    Expr is_inf = bits >= two_to_the_16;

    // Check if the input is a nan, which is anything bigger than an infinity bit pattern
    Expr input_inf_bits = src_bits == 32 ?
                              make_const(bits_t, 0x7f800000) :
                              make_const(bits_t, (uint64_t)0x7ff0000000000000ULL);
    Expr is_nan = bits > input_inf_bits;

    // Denorms are linearly spaced, so we can handle them
    // by scaling up the input as a float and using the
    // existing int-conversion rounding instructions.
    Expr two_to_the_24 = src_bits == 32 ?
                             make_const(bits_t, 0x0c000000) :
                             make_const(bits_t, (uint64_t)0x0180000000000000ULL);
    Expr denorm_bits = cast(u16_t, strict_float(round(reinterpret(float_t, bits + two_to_the_24))));
    Expr inf_bits = make_const(u16_t, 0x7c00);
    Expr nan_bits = make_const(u16_t, 0x7fff);

    // We want to round to nearest even, so we add either
    // 0.5 if the integer part is odd, or 0.4999999 if the
    // integer part is even, then truncate.
    const int float16_mantissa_bits = 10;
    const int input_mantissa_bits = src_bits == 32 ? 23 : 52;
    const int bits_lost = input_mantissa_bits - float16_mantissa_bits;
    bits += (bits >> bits_lost) & 1;
    bits += make_const(bits_t, ((uint64_t)1 << (bits_lost - 1)) - 1);
    bits = cast(u16_t, bits >> bits_lost);

    // Rebias the exponent
    bits -= 0x4000;
    // Truncate the top bits of the exponent
    bits = bits & 0x7fff;
    bits = select(is_denorm, denorm_bits,
                  is_inf, inf_bits,
                  is_nan, nan_bits,
                  cast(u16_t, bits));
    // Recover the sign bit
    bits = bits | cast(u16_t, sign >> (src_bits - 16));
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
        // Most of the intrinsics above return float, so the return type needs
        // adjusting, but some return bool.
        Type t = op->type.is_float() ? Float(32, op->type.lanes()) : op->type;
        Expr e = Call::make(t, it->second, new_args, op->call_type,
                            op->func, op->value_index, op->image, op->param);
        if (op->type.is_float()) {
            e = float_to_float16(e);
        }
        internal_assert(e.type() == op->type);
        return e;
    } else {
        internal_error << "Unknown float16 transcendental: " << Expr(op) << "\n";
        return Expr();
    }
}

Expr lower_float16_cast(const Cast *op) {
    Type src = op->value.type();
    Type dst = op->type;
    Type f32 = Float(32, dst.lanes());
    Type f64 = Float(64, dst.lanes());
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
        if (src.bits() > 32) {
            val = cast(f64, val);
        } else {
            val = cast(f32, val);
        }
        val = float_to_bfloat16(val);
    } else if (dst.is_float() && dst.bits() < 32) {
        internal_assert(dst.bits() == 16);
        if (src.bits() > 32) {
            val = cast(f64, val);
        } else {
            val = cast(f32, val);
        }
        val = float_to_float16(val);
    }

    return cast(dst, val);
}

}  // namespace Internal
}  // namespace Halide
