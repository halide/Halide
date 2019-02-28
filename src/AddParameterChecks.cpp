#include "AddParameterChecks.h"
#include "IROperator.h"
#include "IRVisitor.h"
#include "Substitute.h"
#include "Target.h"

namespace Halide {
namespace Internal {

using std::map;
using std::pair;
using std::string;
using std::vector;

// Find all the externally referenced scalar parameters
class FindParameters : public IRGraphVisitor {
public:
    map<string, Parameter> params;

    using IRGraphVisitor::visit;

    void visit(const Variable *op) override {
        if (op->param.defined()) {
            params[op->name] = op->param;
        }
    }
};

// Insert checks to make sure that parameters are within their
// declared range.
Stmt add_parameter_checks(const vector<Stmt> &preconditions, Stmt s, const Target &t) {

    // First, find all the parameters
    FindParameters finder;
    s.accept(&finder);

    map<string, Expr> replace_with_constrained;
    vector<pair<string, Expr>> lets;

    struct ParamAssert {
        Expr condition;
        Expr value, limit_value;
        string param_name;
    };

    vector<ParamAssert> asserts;

    // Make constrained versions of the params
    for (pair<const string, Parameter> &i : finder.params) {
        Parameter param = i.second;

        if (!param.is_buffer() &&
            (param.min_value().defined() ||
             param.max_value().defined())) {

            string constrained_name = i.first + ".constrained";

            Expr constrained_var = Variable::make(param.type(), constrained_name);
            Expr constrained_value = Variable::make(param.type(), i.first, param);
            replace_with_constrained[i.first] = constrained_var;

            if (param.min_value().defined()) {
                ParamAssert p = {
                    constrained_value >= param.min_value(),
                    constrained_value, param.min_value(),
                    param.name()
                };
                asserts.push_back(p);
                constrained_value = max(constrained_value, param.min_value());
            }

            if (param.max_value().defined()) {
                ParamAssert p = {
                    constrained_value <= param.max_value(),
                    constrained_value, param.max_value(),
                    param.name()
                };
                asserts.push_back(p);
                constrained_value = min(constrained_value, param.max_value());
            }

            lets.push_back({ constrained_name, constrained_value });
        }
    }

    // Replace the params with their constrained version in the rest of the pipeline
    s = substitute(replace_with_constrained, s);

    // Inject the let statements
    for (size_t i = 0; i < lets.size(); i++) {
        s = LetStmt::make(lets[i].first, lets[i].second, s);
    }

    // Inject the assert statements
    for (size_t i = 0; i < asserts.size(); i++) {
        ParamAssert p = asserts[i];
        // Upgrade the types to 64-bit versions for the error call
        Type wider = p.value.type().with_bits(64);
        p.limit_value = cast(wider, p.limit_value);
        p.value       = cast(wider, p.value);

        string error_call_name = "halide_error_param";

        if (p.condition.as<LE>()) {
            error_call_name += "_too_large";
        } else {
            internal_assert(p.condition.as<GE>());
            error_call_name += "_too_small";
        }

        if (wider.is_int()) {
            error_call_name += "_i64";
        } else if (wider.is_uint()) {
            error_call_name += "_u64";
        } else {
            internal_assert(wider.is_float());
            error_call_name += "_f64";
        }

        Expr error = Call::make(Int(32), error_call_name,
                                {p.param_name, p.value, p.limit_value},
                                Call::Extern);

        s = Block::make(AssertStmt::make(p.condition, error), s);
    }

    // The unstructured assertions get checked first (because they
    // have a custom error message associated with them), so prepend
    // them last.
    vector<Stmt> stmts = preconditions;
    stmts.push_back(s);
    return Block::make(stmts);
}

}  // namespace Internal
}  // namespace Halide
