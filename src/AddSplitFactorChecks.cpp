#include "AddSplitFactorChecks.h"
#include "Definition.h"
#include "Function.h"
#include "IR.h"
#include "IROperator.h"
#include "Simplify.h"

namespace Halide {
namespace Internal {

namespace {

void check_split_factor(const Function &f, const std::string &old_var,
                        const std::string &outer, const std::string &inner,
                        const Expr &factor, std::vector<Stmt> *stmts) {
    if (is_positive_const(factor)) {
        return;
    }
    Expr positive = simplify(factor > 0);
    if (is_const_one(positive)) {
        return;
    }
    std::ostringstream factor_str;
    factor_str << factor;
    Expr error = Call::make(Int(32), "halide_error_split_factor_not_positive",
                            {f.name(),
                             split_string(old_var, ".").back(),
                             split_string(outer, ".").back(),
                             split_string(inner, ".").back(),
                             factor_str.str(), factor},
                            Call::Extern);
    stmts->push_back(AssertStmt::make(positive, error));
}

void check_all_split_factors(const Function &f, const Definition &def, std::vector<Stmt> *stmts) {
    const StageSchedule &sched = def.schedule();
    for (const Split &split : sched.splits()) {
        if (split.split_type == Split::SplitVar) {
            check_split_factor(f, split.old_var, split.outer, split.inner,
                               split.factor, stmts);
        }
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
        for (const StorageSplit &split : f.schedule().storage_splits()) {
            check_split_factor(f, split.old_var, split.outer, split.inner,
                               split.factor, &stmts);
        }
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
