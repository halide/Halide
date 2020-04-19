#include "AllocationBoundsInference.h"
#include "Bounds.h"
#include "ExternFuncArgument.h"
#include "Function.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Simplify.h"

namespace Halide {
namespace Internal {

using std::map;
using std::set;
using std::string;
using std::vector;

// Figure out the region touched of each buffer, and deposit them as
// let statements outside of each realize node, or at the top level if
// they're not internal allocations.

class AllocationInference : public IRMutator {
    using IRMutator::visit;

    const map<string, Function> &env;
    const FuncValueBounds &func_bounds;
    set<string> touched_by_extern;

    Stmt visit(const Realize *op) override {
        map<string, Function>::const_iterator iter = env.find(op->name);
        internal_assert(iter != env.end());
        Function f = iter->second;
        const vector<string> f_args = f.args();

        Scope<Interval> empty_scope;
        Box b = box_touched(op->body, op->name, empty_scope, func_bounds);

        Stmt new_body = mutate(op->body);
        Stmt stmt = Realize::make(op->name, op->types, op->memory_type, op->bounds, op->condition, new_body);

        // If the realization is dead and there's is no access to the
        // buffer (e.g. because we're in a specialization), then
        // b.size() may be zero. In this case just drop the realize
        // node.
        if (b.empty() && !op->bounds.empty()) {
            return new_body;
        }

        for (size_t i = 0; i < b.size(); i++) {
            // Get any applicable bound on this dimension
            Bound bound;
            for (size_t j = 0; j < f.schedule().bounds().size(); j++) {
                Bound b = f.schedule().bounds()[j];
                if (f_args[i] == b.var) {
                    bound = b;
                }
            }

            string prefix = op->name + "." + f_args[i];
            string min_name = prefix + ".min_realized";
            string max_name = prefix + ".max_realized";
            string extent_name = prefix + ".extent_realized";
            if (!b[i].is_bounded()) {
                user_error << op->name << " is accessed over an unbounded domain in dimension "
                           << f_args[i] << "\n";
            }
            Expr min, max, extent;
            b[i].min = simplify(b[i].min);
            b[i].max = simplify(b[i].max);
            if (bound.min.defined()) {
                min = bound.min;
            } else {
                min = b[i].min;
            }
            if (bound.extent.defined()) {
                extent = bound.extent;
                max = simplify(min + extent - 1);
            } else {
                max = b[i].max;
                extent = simplify((max - min) + 1);
            }
            if (bound.modulus.defined()) {
                internal_assert(bound.remainder.defined());
                min -= bound.remainder;
                min = (min / bound.modulus) * bound.modulus;
                min += bound.remainder;
                Expr max_plus_one = max + 1;
                max_plus_one -= bound.remainder;
                max_plus_one = ((max_plus_one + bound.modulus - 1) / bound.modulus) * bound.modulus;
                max_plus_one += bound.remainder;
                extent = simplify(max_plus_one - min);
                max = max_plus_one - 1;
            }

            Expr min_var = Variable::make(Int(32), min_name);
            Expr max_var = Variable::make(Int(32), max_name);

            internal_assert(min_var.type() == min.type());
            internal_assert(max_var.type() == max.type());

            Expr error_msg = Call::make(Int(32), "halide_error_explicit_bounds_too_small",
                                        {f_args[i], f.name(), min_var, max_var, b[i].min, b[i].max},
                                        Call::Extern);

            if (bound.min.defined()) {
                stmt = Block::make(AssertStmt::make(min_var <= b[i].min, error_msg), stmt);
            }
            if (bound.extent.defined()) {
                stmt = Block::make(AssertStmt::make(max_var >= b[i].max, error_msg), stmt);
            }

            stmt = LetStmt::make(extent_name, extent, stmt);
            stmt = LetStmt::make(min_name, min, stmt);
            stmt = LetStmt::make(max_name, max, stmt);
        }
        return stmt;
    }

public:
    AllocationInference(const map<string, Function> &e, const FuncValueBounds &fb)
        : env(e), func_bounds(fb) {
        // Figure out which buffers are touched by extern stages
        for (map<string, Function>::const_iterator iter = e.begin();
             iter != e.end(); ++iter) {
            Function f = iter->second;
            if (f.has_extern_definition()) {
                touched_by_extern.insert(f.name());
                for (size_t i = 0; i < f.extern_arguments().size(); i++) {
                    ExternFuncArgument arg = f.extern_arguments()[i];
                    if (!arg.is_func()) continue;
                    Function input(arg.func);
                    touched_by_extern.insert(input.name());
                }
            }
        }
    }
};

// We can strip box_touched declarations here. We're done with
// them. Reconsider this decision if we want to use
// box_touched on extern stages later in lowering. Storage
// folding currently does box_touched too, but it handles extern
// stages specially already.
class StripDeclareBoxTouched : public IRMutator {
    using IRMutator::visit;

    Expr visit(const Call *op) override {
        if (op->is_intrinsic(Call::declare_box_touched)) {
            return 0;
        } else {
            return IRMutator::visit(op);
        }
    }
};

Stmt allocation_bounds_inference(Stmt s,
                                 const map<string, Function> &env,
                                 const FuncValueBounds &fb) {
    s = AllocationInference(env, fb).mutate(s);
    s = StripDeclareBoxTouched().mutate(s);
    return s;
}

}  // namespace Internal
}  // namespace Halide
