#include "StrictifyFloat.h"

#include "Function.h"
#include "IRMutator.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

namespace {

class StrictifyFloat : public IRMutator {
    enum Strictness {
        FastMath,
        StrictFloat,
    } strictness;

    using IRMutator::visit;

    Expr visit(const Call *call) override {
        Strictness new_strictness = strictness;

        if (call->is_intrinsic(Call::strict_float)) {
            new_strictness = StrictFloat;
            any_strict_float |= true;
        }

        ScopedValue<Strictness> save_strictness(strictness, new_strictness);

        if (call->type == type_of<ApproximationPrecision*>()) {
            return call;
        }

        return IRMutator::visit(call);
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

}  // namespace

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
