#include "StrictifyFloat.h"

#include "Function.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRVisitor.h"

namespace Halide {
namespace Internal {

namespace {
class Strictify : public IRMutator {
    using IRMutator::visit;

    template<typename T>
    Expr visit_binop(const T *op, Call::IntrinsicOp intrin) {
        Expr a = mutate(op->a);
        Expr b = mutate(op->b);
        if (a.type().is_float()) {
            return Call::make(op->type, intrin,
                              {std::move(a), std::move(b)}, Call::PureIntrinsic);
        } else {
            return T::make(std::move(a), std::move(b));
        }
    }

    Expr visit(const Add *op) override {
        return visit_binop(op, Call::strict_add);
    }

    Expr visit(const Sub *op) override {
        return visit_binop(op, Call::strict_sub);
    }

    Expr visit(const Div *op) override {
        return visit_binop(op, Call::strict_div);
    }

    Expr visit(const Mul *op) override {
        return visit_binop(op, Call::strict_mul);
    }

    Expr visit(const Min *op) override {
        return visit_binop(op, Call::strict_min);
    }

    Expr visit(const Max *op) override {
        return visit_binop(op, Call::strict_max);
    }

    Expr visit(const LT *op) override {
        return visit_binop(op, Call::strict_lt);
    }

    Expr visit(const LE *op) override {
        return visit_binop(op, Call::strict_le);
    }

    Expr visit(const GT *op) override {
        if (op->a.type().is_float()) {
            return mutate(op->b < op->a);
        } else {
            return IRMutator::visit(op);
        }
    }

    Expr visit(const GE *op) override {
        if (op->a.type().is_float()) {
            return mutate(op->b <= op->a);
        } else {
            return IRMutator::visit(op);
        }
    }

    Expr visit(const EQ *op) override {
        return visit_binop(op, Call::strict_eq);
    }

    Expr visit(const NE *op) override {
        if (op->a.type().is_float()) {
            return !mutate(op->b == op->a);
        } else {
            return IRMutator::visit(op);
        }
    }

    Expr visit(const Cast *op) override {
        if (op->value.type().is_float() &&
            op->type.is_float()) {
            return Call::make(op->type, Call::strict_cast,
                              {mutate(op->value)}, Call::PureIntrinsic);
        } else {
            return IRMutator::visit(op);
        }
    }
};

const std::set<std::string> strict_externs = {
    "is_nan_f16",
    "is_nan_f32",
    "is_nan_f64",
    "is_inf_f16",
    "is_inf_f32",
    "is_inf_f64",
    "is_finite_f16",
    "is_finite_f32",
    "is_finite_f64",
};

// Just check for usage of the intrinsics
class AnyStrictIntrinsics : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Call *call) override {
        if (call->is_strict_float_intrinsic() ||
            strict_externs.count(call->name)) {
            any_strict = true;
        } else {
            IRVisitor::visit(call);
        }
    }

public:
    bool any_strict = false;
};

}  // namespace

Expr strictify_float(const Expr &e) {
    return Strictify{}.mutate(e);
}

Expr unstrictify_float(const Call *op) {
    internal_assert(op->is_strict_float_intrinsic())
        << "Called unstrictify_float on something other than a strict float intrinsic: "
        << Expr(op) << "\n";
    if (op->is_intrinsic(Call::strict_add)) {
        return op->args[0] + op->args[1];
    } else if (op->is_intrinsic(Call::strict_sub)) {
        return op->args[0] - op->args[1];
    } else if (op->is_intrinsic(Call::strict_mul)) {
        return op->args[0] * op->args[1];
    } else if (op->is_intrinsic(Call::strict_div)) {
        return op->args[0] / op->args[1];
    } else if (op->is_intrinsic(Call::strict_min)) {
        return min(op->args[0], op->args[1]);
    } else if (op->is_intrinsic(Call::strict_max)) {
        return max(op->args[0], op->args[1]);
    } else if (op->is_intrinsic(Call::strict_lt)) {
        return op->args[0] < op->args[1];
    } else if (op->is_intrinsic(Call::strict_le)) {
        return op->args[0] <= op->args[1];
    } else if (op->is_intrinsic(Call::strict_eq)) {
        return op->args[0] == op->args[1];
    } else if (op->is_intrinsic(Call::strict_fma)) {
        return op->args[0] * op->args[1] + op->args[2];
    } else if (op->is_intrinsic(Call::strict_cast)) {
        return cast(op->type, op->args[0]);
    } else {
        internal_error << "Missing lowering of strict float intrinsic: "
                       << Expr(op) << "\n";
        return Expr{};
    }
}

bool strictify_float(std::map<std::string, Function> &env, const Target &t) {
    Strictify mutator;
    AnyStrictIntrinsics checker;

    for (auto &iter : env) {
        Function &func = iter.second;
        if (t.has_feature(Target::StrictFloat)) {
            func.mutate(&mutator);
        } else {
            func.accept(&checker);
        }
    }
    return checker.any_strict || t.has_feature(Target::StrictFloat);
}

}  // namespace Internal
}  // namespace Halide
