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

    const map<string, Function> &env;

    void visit(const Realize *op) {
        IRMutator::visit(op);
        op = stmt.as<Realize>();

        map<string, Function>::const_iterator iter = env.find(op->name);
        assert(iter != env.end());
        Function f = iter->second;

        Box b = box_provided(op->body, op->name);

        // The region touched is at least the region required at this
        // loop level of the first stage (this is important for inputs
        // and outputs to extern stages).
        Box required(op->bounds.size());
        for (size_t i = 0; i < required.size(); i++) {
            string prefix = f.name() + ".s0." + f.args()[i];
            required[i] = Interval(Variable::make(Int(32), prefix + ".min"),
                                   Variable::make(Int(32), prefix + ".max"));
        }

        for (size_t i = 0; i < b.size(); i++) {
            if (!b[i].min.defined() || !b[i].max.defined()) {
                std::cerr << "The realization of function " << op->name
                          << " is written to in an unbounded way in dimension " << i << "\n"
                          << "Could not deduce how much space to allocate for it.\n";
                assert(false);
            }
        }

        merge_boxes(b, required);

        assert(b.size() == op->bounds.size());
        for (size_t i = 0; i < b.size(); i++) {
            string prefix = f.name() + "." + f.args()[i];
            string min_name = prefix + ".min_realized";
            string max_name = prefix + ".max_realized";
            string extent_name = prefix + ".extent_realized";
            Expr min = b[i].min, extent = (b[i].max - b[i].min) + 1;
            stmt = LetStmt::make(extent_name, extent, stmt);
            stmt = LetStmt::make(min_name, min, stmt);
            stmt = LetStmt::make(max_name, b[i].max, stmt);
        }
    }

public:
    AllocationInference(const map<string, Function> &e) : env(e) {}
};

Stmt allocation_bounds_inference(Stmt s, const map<string, Function> &env) {
    AllocationInference inf(env);
    s = inf.mutate(s);
    return s;
}

}
}
