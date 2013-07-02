#include "BoundsInference.h"
#include "IRMutator.h"
#include "Scope.h"
#include "Bounds.h"
#include "Debug.h"
#include <sstream>

namespace Halide {
namespace Internal {

using std::string;
using std::ostringstream;
using std::vector;
using std::map;

// Inject let stmts defining the bounds of a function required at each loop level
class BoundsInference : public IRMutator {
public:
    const vector<string> &funcs;
    const map<string, Function> &env;
    Scope<int> in_update;

    BoundsInference(const vector<string> &f, const map<string, Function> &e) : funcs(f), env(e) {}

    using IRMutator::visit;

    virtual void visit(const For *for_loop) {

        // Compute the region required of each function within this loop body
        map<string, Region> regions = regions_called(for_loop->body);

        Stmt body = mutate(for_loop->body);


        debug(3) << "Bounds inference considering loop over " << for_loop->name << '\n';

        // Inject let statements defining those bounds
        for (size_t i = 0; i < funcs.size(); i++) {
            if (in_update.contains(funcs[i])) continue;
            const Region &region = regions[funcs[i]];
            const Function &f = env.find(funcs[i])->second;
            if (region.empty()) continue;
            debug(3) << "Injecting bounds for " << funcs[i] << '\n';
            assert(region.size() == f.args().size() && "Dimensionality mismatch between function and region required");
            for (size_t j = 0; j < region.size(); j++) {
                const string &arg_name = f.args()[j];
                body = LetStmt::make(f.name() + "." + arg_name + ".min", region[j].min, body);
                body = LetStmt::make(f.name() + "." + arg_name + ".extent", region[j].extent, body);
            }
        }

        if (body.same_as(for_loop->body)) {
            stmt = for_loop;
        } else {
            stmt = For::make(for_loop->name, for_loop->min, for_loop->extent, for_loop->for_type, body);
        }
    }

    virtual void visit(const Pipeline *pipeline) {
        Stmt produce = mutate(pipeline->produce);

        Stmt update;
        if (pipeline->update.defined()) {
            // Even though there are calls to a function within the
            // update step of a pipeline, we shouldn't modify the
            // bounds computed - they've already been fixed. Any
            // dependencies required should have been scheduled within
            // the initialization, not the update step, so these
            // bounds can't be of use to anyone anyway.
            in_update.push(pipeline->name, 0);
            update = mutate(pipeline->update);
            in_update.pop(pipeline->name);
        }
        Stmt consume = mutate(pipeline->consume);
        stmt = Pipeline::make(pipeline->name, produce, update, consume);
    }
};

Stmt bounds_inference(Stmt s, const vector<string> &order, const map<string, Function> &env) {
    // Add a outermost::make loop to make sure we get outermost bounds definitions too
    s = For::make("outermost", 0, 1, For::Serial, s);

    s = BoundsInference(order, env).mutate(s);

    // We can remove the loop over root now
    const For *root_loop = s.as<For>();
    assert(root_loop);
    s = root_loop->body;

    // For the output function, the bounds required is the (possibly constrained) size of the buffer
    Function f = env.find(order[order.size()-1])->second;
    Parameter b = f.output_buffer();
    for (size_t i = 0; i < f.args().size(); i++) {
        debug(2) << f.name() << ", " << f.args()[i] << "\n";

        string prefix = f.name() + "." + f.args()[i];
        char dim = '0' + i;

        if (b.min_constraint(i).defined()) {
            s = LetStmt::make(prefix + ".min", b.min_constraint(i), s);
        } else {
            string buf_min_name = f.name() + ".min." + dim;
            Expr buf_min = Variable::make(Int(32), buf_min_name);
            s = LetStmt::make(prefix + ".min", buf_min, s);
        }

        if (b.extent_constraint(i).defined()) {
            s = LetStmt::make(prefix + ".extent", b.extent_constraint(i), s);
        } else {
            string buf_extent_name = f.name() + ".extent." + dim;
            Expr buf_extent = Variable::make(Int(32), buf_extent_name);
            s = LetStmt::make(prefix + ".extent", buf_extent, s);
        }

    }

    return s;
}



}
}
