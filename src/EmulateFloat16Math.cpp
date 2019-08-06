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
     {"is_nan_f16", "is_nan_f32"}};

class RemoveHalfTypes : public IRMutator {
    using IRMutator::visit;

    bool is_narrow_float(Type t) {
        return t.is_float() && t.bits() == 16;
    }

    Expr visit(const FloatImm *op) override {
        if (op->type.is_bfloat()) {
            return make_const(UInt(16), bfloat16_t(op->value).to_bits());
        } else if (op->type.bits() == 16) {
            return make_const(UInt(16), float16_t(op->value).to_bits());
        } else {
            return op;
        }
    }

    template<typename Op>
    Expr visit_binop(Call::IntrinsicOp f16, Call::IntrinsicOp bf16, const Op *op) {
        if (is_narrow_float(op->a.type())) {
            Type out = op->type;
            if (is_narrow_float(out)) {
                out = UInt(out.bits(), out.lanes());
            }
            return Call::make(out,
                              op->type.is_bfloat() ? bf16 : f16,
                              {mutate(op->a), mutate(op->b)},
                              Call::PureIntrinsic);
        } else {
            return IRMutator::visit(op);
        }
    }

    Expr visit(const Add *op) override {
        return visit_binop(Call::add_f16, Call::add_bf16, op);
    }

    Expr visit(const Sub *op) override {
        return visit_binop(Call::sub_f16, Call::sub_bf16, op);
    }

    Expr visit(const Mul *op) override {
        return visit_binop(Call::mul_f16, Call::mul_bf16, op);
    }

    Expr visit(const Div *op) override {
        return visit_binop(Call::div_f16, Call::div_bf16, op);
    }

    Expr visit(const Mod *op) override {
        return visit_binop(Call::mod_f16, Call::mod_bf16, op);
    }

    Expr visit(const Min *op) override {
        return visit_binop(Call::min_f16, Call::min_bf16, op);
    }

    Expr visit(const Max *op) override {
        return visit_binop(Call::max_f16, Call::max_bf16, op);
    }

    Expr visit(const EQ *op) override {
        return visit_binop(Call::eq_f16, Call::eq_bf16, op);
    }

    Expr visit(const LT *op) override {
        return visit_binop(Call::lt_f16, Call::lt_bf16, op);
    }

    Expr visit(const NE *op) override {
        return mutate(!(op->a == op->b));
    }

    Expr visit(const LE *op) override {
        return mutate(!(op->b < op->a));
    }

    Expr visit(const GT *op) override {
        return mutate(op->b < op->a);
    }

    Expr visit(const GE *op) override {
        return mutate(!(op->a < op->b));
    }

    Expr visit(const Cast *op) override {
        Type dst = op->type;
        Type src = op->value.type();

        if (is_narrow_float(dst)) {
            // Go via float
            return Call::make(UInt(16, op->type.lanes()),
                              dst.is_bfloat() ? Call::from_float_bf16 : Call::from_float_f16,
                              {mutate(cast(Float(32, op->type.lanes()), op->value))},
                              Call::PureIntrinsic);
        } else if (is_narrow_float(src)) {
            Expr e = Call::make(Float(32, op->type.lanes()),
                                src.is_bfloat() ? Call::to_float_bf16 : Call::to_float_f16,
                                {mutate(op->value)},
                                Call::PureIntrinsic);
            return cast(dst, e);
        } else {
            return IRMutator::visit(op);
        }
    }

    Expr visit(const Call *op) override {
        Type return_type = op->type;
        std::string name = op->name;
        const Type u16 = UInt(16, return_type.lanes());

        // Eagerly lower lerp if there are any float16s
        // involved, to avoid having to worry about float16 values
        // vs float16 weights.
        if (op->is_intrinsic(Call::lerp) &&
            (is_narrow_float(return_type) || // float16 values
             is_narrow_float(op->args[2].type()))) { // float16 weight
            return mutate(lower_lerp(op->args[0], op->args[1], op->args[2]));
        } else if (op->is_intrinsic(Call::abs) &&
                   is_narrow_float(return_type)) {
            // Deal with abs by bitmasking
            return mutate(op->args[0]) & make_const(u16, 0x7fff);
        }

        // Just rewrite the return type
        if (is_narrow_float(return_type)) {
            return_type = u16;
        }

        std::vector<Expr> new_args(op->args.size());
        bool changed = (return_type != op->type);

        // Mutate the args
        for (size_t i = 0; i < op->args.size(); i++) {
            const Expr &old_arg = op->args[i];
            Expr new_arg = mutate(old_arg);
            if (!new_arg.same_as(old_arg)) changed = true;
            new_args[i] = std::move(new_arg);
        }

        if (!changed) {
            return op;
        }
        return Call::make(return_type, name, new_args, op->call_type,
                          op->func, op->value_index, op->image, op->param);
    };

    Expr visit(const Variable *op) override {
        // Just rewrite the type to uint
        if (is_narrow_float(op->type)) {
            Type u16 = UInt(16, op->type.lanes());
            return Variable::make(u16, op->name, op->image, op->param, op->reduction_domain);
        } else {
            return op;
        }
    }


    Expr visit(const Load *op) override {
        Type t = op->type;

        // Just rewrite the type to uint
        if (is_narrow_float(t)) {
            t = UInt(16, op->type.lanes());
        }

        Expr predicate = mutate(op->predicate);
        Expr index = mutate(op->index);
        if (t == op->type &&
            predicate.same_as(op->predicate) &&
            index.same_as(op->index)) {
            return op;
        }
        return Load::make(t, op->name, std::move(index),
                          op->image, op->param, std::move(predicate),
                          op->alignment);
    }

public:
    using IRMutator::mutate;

    Expr mutate(const Expr &e) override {
        Expr new_e = IRMutator::mutate(e);
        internal_assert(!(is_narrow_float(new_e.type())))
            << "Failed to remove float16: " << new_e << "\n";
        internal_assert(is_narrow_float(e.type()) || (new_e.type() == e.type()))
            << "Incorrectly rewrote type of something that isn't a float16:\n"
            << e << " -> " << new_e << "\n";
        return new_e;
    }

};

class LowerFloat16Intrinsics : public IRMutator {
    using IRMutator::visit;

    template<typename BinOp>
    Expr lower_f16_binop(const Call *op, const BinOp &binop) {
        Expr a = mutate(op->args[0]);
        Expr b = mutate(op->args[1]);
        a = float16_to_float32(a);
        b = float16_to_float32(b);
        Expr result = binop(a, b);
        if (result.type().is_float()) {
            result = float32_to_float16(result);
        }
        return result;
    }

    template<typename BinOp>
    Expr lower_bf16_binop(const Call *op, const BinOp &binop) {
        Expr a = mutate(op->args[0]);
        Expr b = mutate(op->args[1]);
        a = bfloat16_to_float32(a);
        b = bfloat16_to_float32(b);
        Expr result = binop(a, b);
        if (result.type().is_float()) {
            internal_assert(result.type().bits() == 32);
            result = float32_to_bfloat16(result);
        }
        return result;
    }

    Expr visit(const Call *op) override {
        if (op->is_intrinsic(Call::add_f16)) {
            return lower_f16_binop(op, Add::make);
        } else if (op->is_intrinsic(Call::add_bf16)) {
            return lower_bf16_binop(op, Add::make);
        } else if (op->is_intrinsic(Call::sub_f16)) {
            return lower_f16_binop(op, Sub::make);
        } else if (op->is_intrinsic(Call::sub_bf16)) {
            return lower_bf16_binop(op, Sub::make);
        } else if (op->is_intrinsic(Call::mul_f16)) {
            return lower_f16_binop(op, Mul::make);
        } else if (op->is_intrinsic(Call::mul_bf16)) {
            return lower_bf16_binop(op, Mul::make);
        } else if (op->is_intrinsic(Call::div_f16)) {
            return lower_f16_binop(op, Div::make);
        } else if (op->is_intrinsic(Call::div_bf16)) {
            return lower_bf16_binop(op, Div::make);
        } else if (op->is_intrinsic(Call::mod_f16)) {
            return lower_f16_binop(op, Mod::make);
        } else if (op->is_intrinsic(Call::mod_bf16)) {
            return lower_bf16_binop(op, Mod::make);
        } else if (op->is_intrinsic(Call::min_f16)) {
            return lower_f16_binop(op, Min::make);
        } else if (op->is_intrinsic(Call::min_bf16)) {
            return lower_bf16_binop(op, Min::make);
        } else if (op->is_intrinsic(Call::max_f16)) {
            return lower_f16_binop(op, Max::make);
        } else if (op->is_intrinsic(Call::max_bf16)) {
            return lower_bf16_binop(op, Max::make);
        } else if (op->is_intrinsic(Call::eq_f16)) {
            return lower_f16_binop(op, EQ::make);
        } else if (op->is_intrinsic(Call::eq_bf16)) {
            return lower_bf16_binop(op, EQ::make);
        } else if (op->is_intrinsic(Call::lt_f16)) {
            return lower_f16_binop(op, LT::make);
        } else if (op->is_intrinsic(Call::lt_bf16)) {
            return lower_bf16_binop(op, LT::make);
        } else if (op->is_intrinsic(Call::to_float_f16)) {
            return float16_to_float32(mutate(op->args[0]));
        } else if (op->is_intrinsic(Call::to_float_bf16)) {
            return bfloat16_to_float32(mutate(op->args[0]));
        } else if (op->is_intrinsic(Call::from_float_f16)) {
            return float32_to_float16(mutate(op->args[0]));
        } else if (op->is_intrinsic(Call::from_float_bf16)) {
            return float32_to_bfloat16(mutate(op->args[0]));
        } else {
            auto it = transcendental_remapping.find(op->name);
            if (it != transcendental_remapping.end()) {
                std::vector<Expr> new_args(op->args.size());
                for (size_t i = 0; i < op->args.size(); i++) {
                    new_args[i] = mutate(float16_to_float32(op->args[i]));
                }
                Expr e = Call::make(Float(32, op->type.lanes()), it->second, new_args, op->call_type,
                                    op->func, op->value_index, op->image, op->param);
                return float32_to_float16(e);
            } else {
                return IRMutator::visit(op);
            }
        }
    }
};

}  // anonymous namespace

Stmt use_intrinsics_for_float16_math(const Stmt &stmt, const Target &t) {
    // Now replace all half types with uints, and replace all
    // remaining math on them with intrinsics calls. After this we
    // don't expect to see any half types anywhere. Backends can
    // reintroduce them in the implementation of the intrinsics.
    return RemoveHalfTypes().mutate(stmt);
}

bool is_float16_intrinsic(const Call *op) {
    return (op->is_intrinsic(Call::add_f16) ||
            op->is_intrinsic(Call::add_bf16) ||
            op->is_intrinsic(Call::sub_f16) ||
            op->is_intrinsic(Call::sub_bf16) ||
            op->is_intrinsic(Call::mul_f16) ||
            op->is_intrinsic(Call::mul_bf16) ||
            op->is_intrinsic(Call::div_f16) ||
            op->is_intrinsic(Call::div_bf16) ||
            op->is_intrinsic(Call::mod_f16) ||
            op->is_intrinsic(Call::mod_bf16) ||
            op->is_intrinsic(Call::min_f16) ||
            op->is_intrinsic(Call::min_bf16) ||
            op->is_intrinsic(Call::max_f16) ||
            op->is_intrinsic(Call::max_bf16) ||
            op->is_intrinsic(Call::eq_f16) ||
            op->is_intrinsic(Call::eq_bf16) ||
            op->is_intrinsic(Call::lt_f16) ||
            op->is_intrinsic(Call::lt_bf16) ||
            op->is_intrinsic(Call::to_float_f16) ||
            op->is_intrinsic(Call::to_float_bf16) ||
            op->is_intrinsic(Call::from_float_f16) ||
            op->is_intrinsic(Call::from_float_bf16) ||
            transcendental_remapping.find(op->name) != transcendental_remapping.end());
}

Expr lower_float16_intrinsics_to_float32_math(const Expr &e) {
    return LowerFloat16Intrinsics().mutate(e);
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

    return simplify(val);
}

}  // namespace Internal
}  // namespace Halide
