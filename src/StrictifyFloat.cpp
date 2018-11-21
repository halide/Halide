#include "StrictifyFloat.h"

#include "IRMutator.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

class StrictifyFloat : public IRMutator2 {
    bool strict_float_allowed;
    enum Strictness {
        FastMath,
        StrictFloat,
    } strictness;

    using IRMutator2::visit;

    Expr visit(const Call *call) override {
        Strictness new_strictness = strictness;

        if (call->is_intrinsic(Call::strict_float)) {
            user_assert(strict_float_allowed) << "strict_float intrinsic is not allowed unless target has feature 'allow_strict_float' or 'force_strict_float'\n";
            new_strictness = StrictFloat;
            any_strict_float |= true;
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
            case StrictFloat:
                return strict_float(e);
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
    bool any_strict_float{false};

    StrictifyFloat(StrictnessMode mode)
        : strict_float_allowed(mode != NotAllowed),
          strictness((mode == Forced) ? StrictFloat : FastMath) {
         any_strict_float |= (mode == Forced);
     }
};

bool strictify_float(std::map<std::string, Function> &env, const Target &t) {
    bool any_strict_float = false;
    for (auto &iter : env) {
        Function &func = iter.second;

        StrictifyFloat::StrictnessMode mode = StrictifyFloat::Allowed;
        if (t.has_feature(Target::StrictFloat)) {
            mode = StrictifyFloat::Forced;
        }
        // TODO(zalman): Some targets don't allow strict float and we can provide errors for these.

        StrictifyFloat strictify(mode);
        func.mutate(&strictify);
        any_strict_float |= strictify.any_strict_float;
    }
    return any_strict_float;
}

}  // namespace Internal
}  // namespace Halide
