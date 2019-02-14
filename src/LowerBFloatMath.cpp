#include "LowerBFloatMath.h"
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
                const Expr &old_arg = op->args[i];
                Expr new_arg = widen(mutate(old_arg));
                new_args[i] = std::move(new_arg);
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

};

class LowerFloat16Conversions : public IRMutator {
    using IRMutator::visit;

    // Taken from the branchless implementation by Phernost here,
    // which they placed in the public domain:
    // https://stackoverflow.com/questions/1659440/32-bit-to-16-bit-floating-point-conversion

    // That code was modified to round to nearest with ties to even on
    // float -> float16 conversion. The original just rounded down.

    static constexpr int shift = 13;
    static constexpr int shiftSign = 16;

    static constexpr int32_t infN = 0x7F800000; // flt32 infinity
    static constexpr int32_t maxN = 0x477FE000; // max flt16 normal as a flt32
    static constexpr int32_t minN = 0x38800000; // min flt16 normal as a flt32
    static constexpr int32_t signN = 0x80000000; // flt32 sign bit

    static constexpr int32_t infC = infN >> shift;
    static constexpr int32_t nanN = (infC + 1) << shift; // minimum flt16 nan as a flt32
    static constexpr int32_t maxC = maxN >> shift;
    static constexpr int32_t minC = minN >> shift;
    static constexpr int32_t signC = signN >> shiftSign; // flt16 sign bit

    static constexpr int32_t mulN = 0x52000000; // (1 << 23) / minN
    static constexpr int32_t mulC = 0x33800000; // minN / (1 << (23 - shift))

    static constexpr int32_t subC = 0x003FF; // max flt32 subnormal down shifted
    static constexpr int32_t norC = 0x00400; // min flt32 normal down shifted

    static constexpr int32_t maxD = infC - maxC - 1;
    static constexpr int32_t minD = minC - subC - 1;

    // Reinterpret cast helpers
    Expr as_u32(Expr v) {
        return reinterpret(UInt(32, v.type().lanes()), v);
    }

    Expr as_u16(Expr v) {
        return reinterpret(UInt(16, v.type().lanes()), v);
    }

    Expr as_i32(Expr v) {
        return reinterpret(Int(32, v.type().lanes()), v);
    }

    Expr as_f32(Expr v) {
        return reinterpret(Float(32, v.type().lanes()), v);
    }

    // Cast helpers
    Expr to_i32(Expr v) {
        return cast(Int(32, v.type().lanes()), v);
    }

    Expr to_i16(Expr v) {
        return cast(Int(16, v.type().lanes()), v);
    }

    Expr to_f32(Expr v) {
        return cast(Float(32, v.type().lanes()), v);
    }

    Expr bool_to_mask(Expr v) {
        Type t = Int(32, v.type().lanes());
        return select(v, make_const(t, -1), make_const(t, 0));
    }

    // Logical shift right of an i32
    Expr lsr(Expr v, int amt) {
        return as_i32(as_u32(v) >> amt);
    }

    Expr float_to_float16(Expr value) {
        Expr v = as_i32(value);
        Expr sign = v & cast(v.type(), signN);
        v = v ^ sign;
        sign = lsr(sign, shiftSign);
        Expr s = cast(v.type(), mulN);
        s = to_i32(as_f32(s) * as_f32(v));
        v = v ^ ((s ^ v) & bool_to_mask(minN > as_i32(v)));
        v = v ^ ((cast(v.type(), infN) ^ v) & bool_to_mask(infN > as_i32(v) && v > maxN));
        v = v ^ ((cast(v.type(), nanN) ^ v) & bool_to_mask(nanN > as_i32(v) && v > infN));
        Expr dropped_bits_mask = cast(v.type(), (1 << shift) - 1);
        Expr dropped_bits = v & dropped_bits_mask;
        v = lsr(v, shift);
        Expr correction = (dropped_bits + (v & cast(v.type(), 1)) > (1 << (shift - 1)));
        v += correction;
        v = v ^ (((v - maxD) ^ v) & bool_to_mask(v > maxC));
        v = v ^ (((v - minD) ^ v) & bool_to_mask(v > subC));
        v = v | sign;
        v = to_i16(v);
        v = as_u16(v);
        v = common_subexpression_elimination(v);
        return v;
    }

    Expr float16_to_float(Expr value) {
        Expr v = to_i32(as_u16(value));
        Expr sign = v & cast(v.type(), signC);
        v = v ^ sign;
        sign = sign << shiftSign;
        v = v ^ (((v + minD) ^ v) & bool_to_mask(v > subC));
        v = v ^ (((v + maxD) ^ v) & bool_to_mask(v > maxC));
        Expr s = cast(v.type(), mulC);
        s = as_i32(as_f32(s) * v);
        Expr mask = bool_to_mask(norC > v);
        v = v << shift;
        v = v ^ ((s ^ v) & mask);
        v = v | sign;
        v = as_f32(v);
        v = common_subexpression_elimination(v);
        return v;
    }

    Expr visit(const Cast *op) override {
        if (op->type.element_of() == Float(16)) {
            return float_to_float16(to_f32(mutate(op->value)));
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

Stmt lower_float16_math(const Stmt &stmt, const Target &t) {
    Stmt s = stmt;
    s = WidenMath().mutate(s);
    s = LowerBFloatConversions().mutate(s);
    if (!t.has_feature(Target::F16C)) {
        s = LowerFloat16Conversions().mutate(s);
    }
    return s;
}

}  // namespace Internal
}  // namespace Halide
