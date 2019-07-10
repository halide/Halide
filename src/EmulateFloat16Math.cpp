#include "EmulateFloat16Math.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "CSE.h"

namespace Halide {
namespace Internal {

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

class LowerBFloatConversions : public IRMutator {
    using IRMutator::visit;

    Expr bfloat_to_float(Expr e) {
        if (e.type().is_bfloat()) {
            e = reinterpret(e.type().with_code(Type::UInt), e);
        }
        e = cast(UInt(32, e.type().lanes()), e);
        e = e << 16;
        e = reinterpret(Float(32, e.type().lanes()), e);
        return e;
    }

    Expr float_to_bfloat(Expr e) {
        e = reinterpret(UInt(32, e.type().lanes()), e);
        e = e >> 16;
        e = cast(UInt(16, e.type().lanes()), e);
        return e;
    }

    Expr visit(const Cast *op) override {
        if (op->type.is_bfloat()) {
            // Cast via float
            return float_to_bfloat(mutate(cast(Float(32, op->type.lanes()), op->value)));
        } else if (op->value.type().is_bfloat()) {
            return cast(op->type, bfloat_to_float(mutate(op->value)));
        } else {
            return IRMutator::visit(op);
        }
    }

    Expr visit(const Load *op) override {
        if (op->type.is_bfloat()) {
            // Load as uint
            Type load_type = UInt(op->type.bits(), op->type.lanes());
            Expr index = mutate(op->index);
            return Load::make(load_type, op->name, index,
                              op->image, op->param, mutate(op->predicate), op->alignment);
        } else {
            return IRMutator::visit(op);
        }
    }

    Expr visit(const FloatImm *op) override {
        if (op->type.is_bfloat()) {
            return Expr(bfloat16_t(op->value).to_bits());
        } else {
            return op;
        }
    }
};

class LowerFloat16Conversions : public IRMutator {
    using IRMutator::visit;

    Expr float_to_float16(Expr value) {
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
        Expr denorm_bits = cast(u16_t, round(reinterpret(f32_t, bits) * (1 << 24)));
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

    Expr float16_to_float(Expr value) {
        Type f32_t = Float(32, value.type().lanes());
        Type u32_t = UInt(32, value.type().lanes());
        Type i32_t = Int(32, value.type().lanes());
        Type u16_t = UInt(16, value.type().lanes());

        Expr f16_bits = cast(u32_t, reinterpret(u16_t, value));

        Expr magnitude = f16_bits & make_const(u32_t, 0x7fff);
        Expr sign = f16_bits & make_const(u32_t, 0x8000);
        Expr exponent_mantissa = magnitude;

        // Fix denorms
        Expr denorm_bits = count_leading_zeros(magnitude) - 21;
        Expr correction = (denorm_bits << 10) - magnitude * ((1 << denorm_bits) - 1);
        correction = select(reinterpret(i32_t, denorm_bits) <= 0, 0, correction);
        exponent_mantissa -= correction;

        // Fix extreme values
        exponent_mantissa = select(magnitude == 0, 0, // Map zero to zero
                                   magnitude >= 0x7c00, exponent_mantissa | 0x3f800, // Map infinity to infinity
                                   exponent_mantissa + 0x1c000); // Fix the exponent bias otherwise

        Expr f32 = reinterpret(f32_t, (sign << 16) | (exponent_mantissa << 13));
        return common_subexpression_elimination(f32);
    }

    Expr visit(const Cast *op) override {
        if (op->type.element_of() == Float(16)) {
            return float_to_float16(cast(op->type.with_bits(32), mutate(op->value)));
        } else if (op->value.type().element_of() == Float(16)) {
            return cast(op->type, float16_to_float(mutate(op->value)));
        } else {
            return IRMutator::visit(op);
        }
    }

    Expr visit(const Load *op) override {
        if (op->type.is_float() && op->type.bits() < 32) {
            // Load as uint
            Type load_type = UInt(op->type.bits(), op->type.lanes());
            Expr index = mutate(op->index);
            return Load::make(load_type, op->name, index,
                              op->image, op->param, mutate(op->predicate), op->alignment);
        } else {
            return IRMutator::visit(op);
        }
    }
};

}  // anonymous namespace

Stmt emulate_float16_math(const Stmt &stmt, const Target &t) {
    Stmt s = stmt;
    s = WidenMath().mutate(s);
    s = LowerBFloatConversions().mutate(s);
    // LLVM trunk as of 2/22/2019 has bugs in the lowering of float16 conversions math on avx512
    //if (!t.has_feature(Target::F16C)) {
    s = LowerFloat16Conversions().mutate(s);
    //}
    return s;
}

}  // namespace Internal
}  // namespace Halide
