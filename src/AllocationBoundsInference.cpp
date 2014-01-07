#include "AllocationBoundsInference.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "Bounds.h"
#include "CSE.h"
#include "Simplify.h"
#include "IREquality.h"

namespace Halide {
namespace Internal {

using std::map;
using std::string;
using std::vector;
using std::pair;
using std::make_pair;

// Figure out the region touched of each buffer, and deposit them as
// let statements outside of each realize node, or at the top level if
// they're not internal allocations.

class AllocationInference : public IRMutator {
    using IRMutator::visit;

    void visit(const Realize *op) {
        IRMutator::visit(op);
        op = stmt.as<Realize>();

        Box b = box_provided(op->body, op->name);

        assert(b.size() == op->bounds.size());
        for (size_t i = 0; i < b.size(); i++) {
            const Variable *min_var = op->bounds[i].min.as<Variable>();
            const Variable *extent_var = op->bounds[i].extent.as<Variable>();
            assert(min_var && extent_var);
            string max_name = op->name + "." + int_to_string(i) + ".max_realized";
            Expr max_var = Variable::make(Int(32), max_name);
            Expr min = b[i].min, extent = (b[i].max - b[i].min) + 1;
            //min = common_subexpression_elimination(simplify(min));
            //extent = common_subexpression_elimination(simplify(extent));
            stmt = LetStmt::make(extent_var->name, extent, stmt);
            stmt = LetStmt::make(min_var->name, min, stmt);
            stmt = LetStmt::make(max_name, b[i].max, stmt);
        }
    }
};

Stmt allocation_bounds_inference(Stmt s) {
    AllocationInference inf;
    s = inf.mutate(s);
    return s;
}

}
}
