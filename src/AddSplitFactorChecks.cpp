#include "AddSplitFactorChecks.h"
#include "Definition.h"
#include "Function.h"
#include "IR.h"
#include "IROperator.h"
#include "Simplify.h"

namespace Halide {
namespace Internal {

namespace {

void check_all_split_factors(const Function &f, const Definition &def, std::vector<Stmt> *stmts) {
    const StageSchedule &sched = def.schedule();
    for (const Split &split : sched.splits()) {
        if (split.split_type != Split::SplitVar) {
            continue;
        }
        if (is_positive_const(split.factor)) {
            // Common-case optimization
            continue;
        }
        Expr positive = simplify(split.factor > 0);
        if (is_const_one(positive)) {
            // We statically proved it
            continue;
        }
        // We need a runtime check that says: if the condition is
        // entered, the split factor will be positive. We can still
        // assume the pipeline preconditions, because they will be
        // checked before this.
        std::ostringstream factor_str;
        factor_str << split.factor;
        Expr error = Call::make(Int(32), "halide_error_split_factor_not_positive",
                                {f.name(),
                                 split_string(split.old_var, ".").back(),
                                 split_string(split.outer, ".").back(),
                                 split_string(split.inner, ".").back(),
                                 factor_str.str(), split.factor},
                                Call::Extern);
        stmts->push_back(AssertStmt::make(positive, error));
    }

    for (const auto &s : def.specializations()) {
        check_all_split_factors(f, s.definition, stmts);
    }
}

}  // namespace

Stmt add_split_factor_checks(const Stmt &s, const std::map<std::string, Function> &env) {
    // Check split factors are strictly positive
    std::vector<Stmt> stmts;

    for (const auto &p : env) {
        const Function &f = p.second;
        check_all_split_factors(f, f.definition(), &stmts);
        for (const auto &u : f.updates()) {
            check_all_split_factors(f, u, &stmts);
        }
    }

    stmts.push_back(s);
    return Block::make(stmts);
}

}  // namespace Internal
}  // namespace Halide
