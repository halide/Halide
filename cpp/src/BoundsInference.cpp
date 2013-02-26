#include "BoundsInference.h"
#include "IRMutator.h"
#include "Scope.h"
#include "Bounds.h"
#include "Log.h"
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
        map<string, Region> regions = regions_required(for_loop->body);
        
        Stmt body = mutate(for_loop->body);


        log(3) << "Bounds inference considering loop over " << for_loop->name << '\n';

        // Inject let statements defining those bounds
        for (size_t i = 0; i < funcs.size(); i++) {
            if (in_update.contains(funcs[i])) continue;
            const Region &region = regions[funcs[i]];
            const Function &f = env.find(funcs[i])->second;
            if (region.empty()) continue;
            log(3) << "Injecting bounds for " << funcs[i] << '\n';
            assert(region.size() == f.args().size() && "Dimensionality mismatch between function and region required");
            for (size_t j = 0; j < region.size(); j++) {
                const string &arg_name = f.args()[j];
                body = new LetStmt(f.name() + "." + arg_name + ".min", region[j].min, body);
                body = new LetStmt(f.name() + "." + arg_name + ".extent", region[j].extent, body);
            }
        }

        if (body.same_as(for_loop->body)) {
            stmt = for_loop;
        } else {
            stmt = new For(for_loop->name, for_loop->min, for_loop->extent, for_loop->for_type, body);
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
        stmt = new Pipeline(pipeline->name, produce, update, consume);
    }
};

Stmt bounds_inference(Stmt s, const vector<string> &order, const map<string, Function> &env) {
    // Add a new outermost loop to make sure we get outermost bounds definitions too
    s = new For("outermost", 0, 1, For::Serial, s);

    s = BoundsInference(order, env).mutate(s);

    // We can remove the loop over root now
    const For *root_loop = s.as<For>();
    assert(root_loop);
    s = root_loop->body;    

    // For the output function, the bounds required is the size of the buffer
    Function f = env.find(order[order.size()-1])->second;
    for (size_t i = 0; i < f.args().size(); i++) {
        log(2) << f.name() << ", " << f.args()[i] << "\n";
        ostringstream buf_min_name, buf_extent_name;
        buf_min_name << f.name() << ".min." << i;
        buf_extent_name << f.name() << ".extent." << i;
        Expr buf_min = new Variable(Int(32), buf_min_name.str());
        Expr buf_extent = new Variable(Int(32), buf_extent_name.str());
        s = new LetStmt(f.name() + "." + f.args()[i] + ".min", buf_min, s);
        s = new LetStmt(f.name() + "." + f.args()[i] + ".extent", buf_extent, s);
    }   

    return s;
}



}
}
