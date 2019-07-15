#include "EmulateFloat16Math.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "CSE.h"

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
    e = strict_float(e);
    e = reinterpret(UInt(32, e.type().lanes()), e);
    // We want to round ties to even, so before truncating either
    // add 0x8000 (0.5) to odd numbers or 0x7fff (0.499999) to
    // even numbers.
    e += 0x7fff + ((e >> 16) & 1);
    e = (e >> 16);
    e = cast(UInt(16, e.type().lanes()), e);
    return e;
}


Expr float16_to_float32(Expr value) {
    value = strict_float(value);
    Type f32_t = Float(32, value.type().lanes());
    Type u32_t = UInt(32, value.type().lanes());
    Type u16_t = UInt(16, value.type().lanes());

    Expr f16_bits = reinterpret(u16_t, value);

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

    // Test the endpoints
    Expr bits = reinterpret(u32_t, value);

    // Extract the sign bit
    Expr sign = bits & make_const(u32_t, 0x80000000);
    bits = bits ^ sign;

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

// Widen all (b)float16 math to float math
class WidenMath : public IRMutator {
    using IRMutator::visit;

    bool needs_widening(Type t) {
        return t.is_bfloat() || (t.is_float() && t.bits() < 32);
    }

    Expr widen(Expr e) {
        if (needs_widening(e.type())) {
            return cast(Float(32, e.type().lanes()), e);
        } else {
            return e;
        }
    }

    template<typename Op>
    Expr visit_bin_op(const Op *op) {
        Expr a = widen(mutate(op->a));
        Expr b = widen(mutate(op->b));
        return cast(op->type, Op::make(std::move(a), std::move(b)));
    }

    Expr visit(const Add *op) override { return visit_bin_op(op); }
    Expr visit(const Sub *op) override { return visit_bin_op(op); }
    Expr visit(const Mod *op) override { return visit_bin_op(op); }
    Expr visit(const Mul *op) override { return visit_bin_op(op); }
    Expr visit(const Div *op) override { return visit_bin_op(op); }
    Expr visit(const LE *op) override { return visit_bin_op(op); }
    Expr visit(const LT *op) override { return visit_bin_op(op); }
    Expr visit(const GE *op) override { return visit_bin_op(op); }
    Expr visit(const GT *op) override { return visit_bin_op(op); }
    Expr visit(const Min *op) override { return visit_bin_op(op); }
    Expr visit(const Max *op) override { return visit_bin_op(op); }

    Expr visit(const Call *op) override {
        if (op->call_type == Call::PureIntrinsic) {
            std::vector<Expr> new_args(op->args.size());

            // Mutate the args
            for (size_t i = 0; i < op->args.size(); i++) {
                new_args[i] = widen(mutate(op->args[i]));
            }

            Type t = op->type;
            if (needs_widening(t)) {
                t = Float(32, op->type.lanes());
            }
            Expr ret = Call::make(t, op->name, new_args, op->call_type,
                                  op->func, op->value_index, op->image, op->param);
            return cast(op->type, ret);
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const For *op) override {
        // Check the device_api and only enter body if the device does
        // not support native (b)float16 math. Currently no devices
        // support (b)float16 math, so we always enter the body.
        return IRMutator::visit(op);
    }
};

}  // anonymous namespace

Stmt emulate_float16_math(const Stmt &stmt, const Target &t) {
    Stmt s = stmt;
    s = WidenMath().mutate(s);
    return s;
}

}  // namespace Internal
}  // namespace Halide
