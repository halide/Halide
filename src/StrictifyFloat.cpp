#include "StrictifyFloat.h"

#include "IRMutator.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

class StrictifyFloat : public IRMutator {
    bool strict_float_allowed;
    enum Strictness {
        FastMath,
        StrictFloatFirst,
        StrictFloat,
    } strictness;

    struct StrictnessExprCompare {
        bool operator()(const std::pair<Strictness, Expr> &a,
                        const std::pair<Strictness, Expr> &b) const {
          return ((int)a.first < (int)b.first) ||
                 (a.first == b.first && (a.second.get() < b.second.get()));
        }
    };

    struct StrictnessStmtCompare {
        bool operator()(const std::pair<Strictness, Stmt> &a,
                        const std::pair<Strictness, Stmt> &b) const {
            return ((int)a.first < (int)b.first) ||
                   (a.first == b.first && Stmt::Compare()(a.second, b.second));
        }
    };

    std::map<std::pair<Strictness, Expr>, Expr, StrictnessExprCompare> expr_replacements;
    std::map<std::pair<Strictness, Stmt>, Stmt, StrictnessStmtCompare> stmt_replacements;

    using IRMutator::visit;

    Expr visit(const Call *call) override {
        Strictness new_strictness = strictness;

        if (call->is_intrinsic(Call::strict_float)) {
            user_assert(strict_float_allowed) << "strict_float intrinsic is not allowed unless target has feature 'allow_strict_float' or 'force_strict_float'\n";
            new_strictness = StrictFloatFirst;
            any_strict_float |= true;
        }

        ScopedValue<Strictness> save_strictness(strictness, new_strictness);

        return IRMutator::visit(call);
    }

public:
    Expr mutate(const Expr &expr) override {
        if (!expr.defined()) {
            return expr;
        }

        auto p = expr_replacements.emplace(std::make_pair(strictness, expr), Expr());
        if (p.second) {
            Expr e;
            {
                Strictness new_strictness = (strictness == StrictFloatFirst) ? StrictFloat: strictness;
                ScopedValue<Strictness> save_strictness(strictness, new_strictness);

                e = IRMutator::mutate(expr);
            }

            if (e.type().is_float()) {
                switch (strictness) {
                case FastMath:
                case StrictFloatFirst:
                    break;
                case StrictFloat:
                    const Call *call = e.as<Call>();
                    if (call == nullptr || !call->is_intrinsic(Call::strict_float)) {
                        e = strict_float(e);
                    }
                    break;
                }
            }
            p.first->second = std::move(e);
        }

        return p.first->second;
    }

    Stmt mutate(const Stmt &s) override {
        if (!s.defined()) {
            return s;
        }
        auto p = stmt_replacements.emplace(std::make_pair(strictness, s), Stmt());
        if (p.second) {
            // N.B: Inserting into a map (as the recursive mutate call
            // does), does not invalidate existing iterators.
            p.first->second = IRMutator::mutate(s);
        }
        return p.first->second;
    }

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

Expr strictify_float(Expr e) {
    StrictifyFloat strictify(StrictifyFloat::Allowed);
    return strictify.mutate(e);
}
  
}  // namespace Internal
}  // namespace Halide
