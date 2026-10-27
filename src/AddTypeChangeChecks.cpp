#include "AddTypeChangeChecks.h"
#include "Function.h"
#include "IR.h"
#include "IROperator.h"
#include "Schedule.h"
#include "Simplify.h"

namespace Halide {
namespace Internal {

Stmt add_type_change_checks(const Stmt &s, const std::map<std::string, Function> &env) {
    std::vector<Stmt> stmts;

    for (const auto &p : env) {
        const Function &f = p.second;
        for (const auto &[condition, message] : f.schedule().type_change_checks()) {
            if (!condition.defined()) {
                continue;
            }
            Expr proven = simplify(condition);
            if (is_const_one(proven)) {
                // Statically proven; no runtime check needed.
                continue;
            }
            Expr error = requirement_failed_error(condition, {Expr(message)});
            stmts.push_back(AssertStmt::make(condition, error));
        }
    }

    stmts.push_back(s);
    return Block::make(stmts);
}

}  // namespace Internal
}  // namespace Halide
