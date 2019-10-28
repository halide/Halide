#include "StrictifyFloat.h"

#include <unordered_set>

#include "IRMutator.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

class StrictifyFloat : public IRMutator {
    enum Strictness {
        FastMath,
        StrictFloat,
    } strictness;

    const std::unordered_set<std::string> strict_required_calls = {
        "is_finite_f16",
        "is_finite_f32",
        "is_finite_f64",
        "is_inf_f16",
        "is_inf_f32",
        "is_inf_f64",
        "is_nan_f16",
        "is_nan_f32",
        "is_nan_f64",
    };

    using IRMutator::visit;

    Expr visit(const Call *call) override {
        Strictness new_strictness = strictness;

        if (call->is_intrinsic(Call::strict_float)) {
            new_strictness = StrictFloat;
            any_strict_float |= true;
        }

        ScopedValue<Strictness> save_strictness(strictness, new_strictness);

        Expr result = IRMutator::visit(call);
        if (strict_required_calls.count(call->name)) {
            const Call *c = result.as<Call>();
            internal_assert(c != nullptr);
            internal_assert(c->name == call->name);
            for (const Expr &e : c->args) {
                const Call *strict = e.as<Call>();
                if (!strict || !strict->is_intrinsic(Call::strict_float)) {
                    user_error
                        << c->name << "() may only be used on Exprs "
                           "that will evaluated in strict_float mode; either wrap the Expr in strict_float() "
                           "or add strict_float to your Target flags.\n"
                        << e << "\n";
                }
            }
        }
        return result;
    }

    using IRMutator::mutate;

    Expr mutate(const Expr &expr) override {
        if (!expr.defined()) {
            return expr;
        }
        Expr e = IRMutator::mutate(expr);
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
        Allowed,
        Forced
    };
    bool any_strict_float{false};

    StrictifyFloat(StrictnessMode mode)
        : strictness((mode == Forced) ? StrictFloat : FastMath) {
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
