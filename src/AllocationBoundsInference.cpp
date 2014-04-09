#include "AllocationBoundsInference.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Bounds.h"
#include "Simplify.h"

namespace Halide {
namespace Internal {

using std::map;
using std::string;
using std::vector;
using std::pair;
using std::make_pair;
using std::set;

// Figure out the region touched of each buffer, and deposit them as
// let statements outside of each realize node, or at the top level if
// they're not internal allocations.

class AllocationInference : public IRMutator {
    using IRMutator::visit;

    const map<string, Function> &env;
    const FuncValueBounds &func_bounds;
    set<string> touched_by_extern;

    void visit(const Realize *op) {
        map<string, Function>::const_iterator iter = env.find(op->name);
        assert (iter != env.end());
        Function f = iter->second;

        Box b = box_touched(op->body, op->name, Scope<Interval>(), func_bounds);

        if (touched_by_extern.count(f.name())) {
            // The region touched is at least the region required at this
            // loop level of the first stage (this is important for inputs
            // and outputs to extern stages).
            Box required(op->bounds.size());
            for (size_t i = 0; i < required.size(); i++) {
                string prefix = op->name + ".s0." + f.args()[i];
                required[i] = Interval(Variable::make(Int(32), prefix + ".min"),
                                       Variable::make(Int(32), prefix + ".max"));
            }

            merge_boxes(b, required);
        }

        Stmt new_body = mutate(op->body);

        // If f is used by an extern, make a buffer for it (TODO: This
        // is similar to the buffer made by the GPU backend, should
        // unify the two ideas).
        if (touched_by_extern.count(f.name())) {
            vector<Expr> mins(f.args().size());
            vector<Expr> extents(f.args().size());
            vector<Expr> strides(f.args().size());
            for (size_t i = 0; i < f.args().size(); i++) {
                mins[i] = Variable::make(Int(32), f.name() + "." + f.args()[i] + ".min_realized");
                extents[i] = Variable::make(Int(32), f.name() + "." + f.args()[i] + ".extent_realized");
            }

            for (int i = 0; i < f.outputs(); i++) {
                string buffer_name = f.name();
                if (f.outputs() > 1) {
                    buffer_name += "." + int_to_string(i);
                }
                buffer_name += ".buffer";
                Expr min_point = Call::make(f, mins, i);
                Expr address = Call::make(Handle(), Call::address_of, vec(min_point), Call::Intrinsic);
                vector<Expr> buffer_fields(f.dimensions()*3 + 2);
                buffer_fields[0] = address;
                buffer_fields[1] = f.output_types()[i].bytes();
                for (int j = 0; j < f.dimensions(); j++) {
                    buffer_fields[j*3 + 2] = mins[j];
                    buffer_fields[j*3 + 3] = extents[j];

                    Expr stride;
                    if (f.outputs() > 1) {
                        stride = Variable::make(Int(32),
                                                f.name() + "." +
                                                int_to_string(i) + ".stride." +
                                                int_to_string(j));
                    } else {
                        stride = Variable::make(Int(32),
                                                f.name() + ".stride." +
                                                int_to_string(j));
                    }
                    buffer_fields[j*3 + 4] = stride;

                }
                Expr buffer = Call::make(Handle(), Call::create_buffer_t, buffer_fields, Call::Intrinsic);
                new_body = LetStmt::make(buffer_name, buffer, new_body);
            }
        }

        stmt = Realize::make(op->name, op->types, op->bounds, new_body);

        assert(b.size() == op->bounds.size());
        for (size_t i = 0; i < b.size(); i++) {
            string prefix = op->name + "." + f.args()[i];
            string min_name = prefix + ".min_realized";
            string max_name = prefix + ".max_realized";
            string extent_name = prefix + ".extent_realized";
            Expr min = simplify(b[i].min);
            Expr max = simplify(b[i].max);
            Expr extent = simplify((max - min) + 1);
            stmt = LetStmt::make(extent_name, extent, stmt);
            stmt = LetStmt::make(min_name, min, stmt);
            stmt = LetStmt::make(max_name, max, stmt);
        }

    }

public:
    AllocationInference(const map<string, Function> &e, const FuncValueBounds &fb) :
        env(e), func_bounds(fb) {
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

Stmt allocation_bounds_inference(Stmt s,
                                 const map<string, Function> &env,
                                 const FuncValueBounds &fb) {
    AllocationInference inf(env, fb);
    s = inf.mutate(s);
    return s;
}

}
}
