#include "LowerBFloatMath.h"
#include "IRMutator.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

namespace {
class LowerBFloatMath : public IRMutator {
public:
    using IRMutator::mutate;

    Expr mutate(const Expr &e) override {
        Expr new_e = IRMutator::mutate(e);
        if (e.type().is_bfloat()) {
            Type expected = UInt(16, e.type().lanes());
            internal_assert(new_e.type() == expected)
                << "Did not successfully remove bfloat math: " << e << " -> " << new_e << "\n";
        }
        return new_e;
    }

protected:
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

    template<typename Op>
    Expr visit_bin_op(const Op *op) {
        Expr a = mutate(op->a);
        Expr b = mutate(op->b);
        if (op->a.type().is_bfloat()) {
            a = bfloat_to_float(a);
            b = bfloat_to_float(b);
            Expr result = Op::make(std::move(a), std::move(b));
            if (op->type.is_bfloat()) {
                result = float_to_bfloat(result);
            }
            return result;
        } else {
            return Op::make(std::move(a), std::move(b));
        }
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

    Expr visit(const FloatImm *op) override {
        if (op->type.is_bfloat()) {
            return Expr(bfloat16_t(op->value).to_bits());
        } else {
            return op;
        }
    }

    Expr visit(const Call *op) override {
        if (op->call_type == Call::PureIntrinsic) {
            std::vector<Expr> new_args(op->args.size());

            // Mutate the args
            for (size_t i = 0; i < op->args.size(); i++) {
                const Expr &old_arg = op->args[i];
                Expr new_arg = mutate(old_arg);
                if (old_arg.type().is_bfloat()) {
                    new_arg = bfloat_to_float(new_arg);
                }
                new_args[i] = std::move(new_arg);
            }

            Type t = op->type;
            if (t.is_bfloat()) {
                t = Float(32, op->type.lanes());
            }
            Expr ret = Call::make(t, op->name, new_args, op->call_type,
                                  op->func, op->value_index, op->image, op->param);
            if (op->type.is_bfloat()) {
                return float_to_bfloat(ret);
            } else {
                return ret;
            }
        } else {
            return IRMutator::visit(op);
        }
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
            // Load as uint16_t then widen to float
            Expr index = mutate(op->index);
            return Load::make(op->type.with_code(Type::UInt), op->name, index,
                              op->image, op->param, mutate(op->predicate), op->alignment);
        } else {
            return IRMutator::visit(op);
        }
    }

    // TODO: Call args and return values

    Stmt visit(const For *op) override {
        // Check the device_api and only enter body if the device does
        // not support bfloat16 math. Currently no devices support
        // bfloat16 math, so we always enter the body.
        return IRMutator::visit(op);
    }
};

}  // anonymous namespace

Stmt lower_bfloat_math(Stmt s) {
    return LowerBFloatMath().mutate(s);
}

}  // namespace Internal
}  // namespace Halide
