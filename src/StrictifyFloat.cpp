#include "StrictifyFloat.h"

#include "IRMutator.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

class StrictifyFloat : public IRMutator2 {
    bool strict_float_allowed;
    enum Strictness {
        FastMath,
        NoFloatSimplify,
        StrictFloat,
    } strictness;

    using IRMutator2::visit;

    Expr visit(const Call *call) override {
        Strictness new_strictness = strictness;

        if (call->is_intrinsic(Call::strict_float)) {
            user_assert(strict_float_allowed) << "strict_float intrinsic is not allowed unless target has feature 'allow_strict_float' or 'force_strict_float'\n";
            new_strictness = StrictFloat;
        } else if (call->is_intrinsic(Call::no_float_simplify) && new_strictness != StrictFloat) {
            new_strictness = NoFloatSimplify;
        }

        ScopedValue<Strictness> save_strictness(strictness, new_strictness);

        return IRMutator2::visit(call);
    }

    using IRMutator2::mutate;

    Expr mutate(const Expr &expr) override {
        if (!expr.defined()) {
            return expr;
        }
        Expr e = IRMutator2::mutate(expr);
        if (e.type().is_float()) {
            switch (strictness) {
            case FastMath:
                return e;
                break;
            case NoFloatSimplify:
                return no_float_simplify(e);
                break;
            case StrictFloat:
                return strict_float(e);
                break;
            }
        }
        return e;
    }

public:
    enum StrictnessMode {
        NotAllowed,
        Allowed,
        Forced
    };

    StrictifyFloat(StrictnessMode mode)
        : strict_float_allowed(mode != NotAllowed),
          strictness((mode == Forced) ? StrictFloat : FastMath) {
     }
};

void strictify_float(std::map<std::string, Function> &env, const Target &t) {
    for (auto &iter : env) {
        Function &func = iter.second;

        StrictifyFloat::StrictnessMode mode = StrictifyFloat::NotAllowed;
        if (t.has_feature(Target::ForceStrictFloat)) {
            mode = StrictifyFloat::Forced;
        } else if (t.has_feature(Target::AllowStrictFloat)) {
            mode = StrictifyFloat::Allowed;
        }

        StrictifyFloat strictify(mode);
        func.mutate(&strictify);
    }
}

}
}
