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

    Expr visit(const Call *op) override {
        if (op->call_type == Call::PureExtern &&
            (op->name == "sqrt_f16" ||
             op->name == "sqrt_f32" ||
             op->name == "sqrt_f64")) {
            return Call::make(op->type, Call::strict_sqrt,
                              {mutate(op->args[0])}, Call::PureIntrinsic);
        } else {
            return IRMutator::visit(op);
        }
    }
};

class Unstrictify : public IRMutator {
    using IRMutator::visit;

    Expr visit(const Call *op) override {
        if (op->is_intrinsic(Call::strict_add)) {
            return mutate(op->args[0] + op->args[1]);
        } else if (op->is_intrinsic(Call::strict_sub)) {
            return mutate(op->args[0] - op->args[1]);
        } else if (op->is_intrinsic(Call::strict_mul)) {
            return mutate(op->args[0] * op->args[1]);
        } else if (op->is_intrinsic(Call::strict_div)) {
            return mutate(op->args[0] / op->args[1]);
        } else if (op->is_intrinsic(Call::strict_min)) {
            return mutate(min(op->args[0], op->args[1]));
        } else if (op->is_intrinsic(Call::strict_max)) {
            return mutate(max(op->args[0], op->args[1]));
        } else if (op->is_intrinsic(Call::strict_sqrt)) {
            Expr e;
            if (op->type.bits() == 64) {
                e = Call::make(op->type, "sqrt_f64", op->args, Call::PureExtern);
            } else if (op->type.bits() == 16) {
                e = Call::make(op->type, "sqrt_f16", op->args, Call::PureExtern);
            } else {
                e = Call::make(op->type, "sqrt_f32", op->args, Call::PureExtern);
            }
            return mutate(e);
        } else if (op->is_intrinsic(Call::strict_lt)) {
            return op->args[0] < op->args[1];
        } else if (op->is_intrinsic(Call::strict_le)) {
            return op->args[0] <= op->args[1];
        } else if (op->is_intrinsic(Call::strict_eq)) {
            return op->args[0] == op->args[1];
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
        if (call->is_intrinsic({Call::strict_add,
                                Call::strict_div,
                                Call::strict_max,
                                Call::strict_min,
                                Call::strict_mul,
                                Call::strict_sqrt,
                                Call::strict_sub,
                                Call::strict_lt,
                                Call::strict_le,
                                Call::strict_eq}) ||
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

Expr unstrictify_float(const Expr &e) {
    return Unstrictify{}.mutate(e);
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
